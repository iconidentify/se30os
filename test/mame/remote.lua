-- Remote control for MAME: polls /work/ctl/cmd for commands, runs them in
-- emulated time, and writes /work/ctl/status ("busy"/"idle <time>").
local CMD, STATUS = "/work/ctl/cmd", "/work/ctl/status"
local queue, keys = {}, nil
local mouse_acc = {0, 0}
local cur = nil          -- current action state
local function now() return manager.machine.time:as_double() end
local function port(tag) return manager.machine.ioport.ports[tag] end
local function mfield(axis) return port(":macadb:MOUSE" .. axis).fields[axis == 1 and "Mouse X" or "Mouse Y"] end
local function button() return port(":macadb:MOUSE0").fields["Mouse Button 0"] end

local function build_keys()
    keys = {}
    for tag, p in pairs(manager.machine.ioport.ports) do
        if tag:find(":macadb:KEY") then
            for name, f in pairs(p.fields) do
                local k = name:match("^(%S)  %S$") or name:lower()
                keys[k] = f
            end
        end
    end
    keys["cmd"] = keys["command / open apple"]
    keys["option"] = keys["option / solid apple"]
    keys["return"] = keys["return"]
    keys["."] = keys["."]
end

local function status(s)
    local f = io.open(STATUS, "w"); if f then f:write(s .. "\n"); f:close() end
end

local function poll()
    local f = io.open(CMD, "r")
    if not f then return end
    for line in f:lines() do
        if line:match("%S") then queue[#queue + 1] = line end
    end
    f:close(); os.remove(CMD)
    status("busy")
end

local function start(line)
    local a = {}
    for w in line:gmatch("%S+") do a[#a + 1] = w end
    local op = a[1]
    if op == "mouse" then
        cur = {op = "mouse", dx = tonumber(a[2]), dy = tonumber(a[3]), step = tonumber(a[4] or 4)}
    elseif op == "down" or op == "up" then
        button():set_value(op == "down" and 1 or 0)
        cur = {op = "wait", t = now() + 0.15}
    elseif op == "click" then
        button():set_value(1)
        cur = {op = "release", t = now() + 0.15, f = {button()}}
    elseif op == "key" then
        -- key <k> [mod ...]: hold mods + key briefly
        local fs = {}
        for i = 3, #a do fs[#fs + 1] = keys[a[i]] end
        fs[#fs + 1] = keys[a[2]]
        for _, f in ipairs(fs) do f:set_value(1) end
        cur = {op = "release", t = now() + 0.2, f = fs}
    elseif op == "type" then
        manager.machine.natkeyboard:post(line:sub(6))
        cur = {op = "typing"}
    elseif op == "wait" then
        cur = {op = "wait", t = now() + tonumber(a[2])}
    elseif op == "snap" then
        manager.machine.screens[":screen"]:snapshot(a[2] .. ".png")
        cur = nil
    elseif op == "kmap" then
        -- kmap CODE...: press keys by setting their bits in Mac OS's KeyMap
        -- (0x174), for code that polls it before the ROM's ADB driver works
        -- in MAME. Codes are hex virtual key codes, pressed together.
        local mem = manager.machine.devices[":maincpu"].spaces["program"]
        local codes = {}
        for i = 2, #a do
            local k = tonumber(a[i], 16)
            codes[#codes + 1] = k
            local addr = 0x174 + (k >> 3)
            mem:write_u8(addr, mem:read_u8(addr) | (1 << (k & 7)))
        end
        cur = {op = "kmaprelease", t = now() + 0.15, codes = codes}
    elseif op == "lua" then
        -- lua FILE: run a debugging script from the work directory
        local ok, err = pcall(dofile, "/work/" .. a[2])
        if not ok then print("remote: " .. tostring(err)) end
        cur = nil
    elseif op == "quit" then
        manager.machine:exit()
    else
        print("remote: unknown command " .. line); cur = nil
    end
end

sub = emu.add_machine_frame_notifier(function()
    if not keys then build_keys(); status("idle 0") end
    poll()
    if cur then
        if cur.op == "mouse" then
            -- move a few units per frame so the Mac sees small, steady
            -- deltas. MAME's ADB mouse drops a packet identical to the one
            -- before it (it has already consumed the motion), so alternate
            -- the step size to keep consecutive packets different.
            cur.flip = not cur.flip
            local st = cur.step + (cur.flip and 1 or 0)
            local sx = math.max(-st, math.min(st, cur.dx))
            local sy = math.max(-st, math.min(st, cur.dy))
            mouse_acc[1] = mouse_acc[1] + sx; mouse_acc[2] = mouse_acc[2] + sy
            mfield(1):set_value(mouse_acc[1] % 256); mfield(2):set_value(mouse_acc[2] % 256)
            cur.dx = cur.dx - sx; cur.dy = cur.dy - sy
            if cur.dx == 0 and cur.dy == 0 then cur = {op = "wait", t = now() + 0.1} end
        elseif cur.op == "release" then
            if now() >= cur.t then
                for _, f in ipairs(cur.f) do f:set_value(0) end
                cur = {op = "wait", t = now() + 0.15}
            end
        elseif cur.op == "kmaprelease" then
            if now() >= cur.t then
                local mem = manager.machine.devices[":maincpu"].spaces["program"]
                for _, k in ipairs(cur.codes) do
                    local addr = 0x174 + (k >> 3)
                    mem:write_u8(addr, mem:read_u8(addr) & ~(1 << (k & 7)))
                end
                cur = {op = "wait", t = now() + 0.15}
            end
        elseif cur.op == "wait" then
            if now() >= cur.t then cur = nil end
        elseif cur.op == "typing" then
            if manager.machine.natkeyboard.empty then cur = {op = "wait", t = now() + 0.3} end
        end
        return
    end
    if #queue > 0 then
        start(table.remove(queue, 1))
    elseif cur == nil then
        status(string.format("idle %.1f", now()))
    end
end)
