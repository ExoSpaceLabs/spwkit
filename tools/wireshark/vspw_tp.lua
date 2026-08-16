-- SPDX-License-Identifier: Apache-2.0
-- VSPW-TP v1 Wireshark Lua dissector for SpWKit development/integration.
--
-- This file is tooling only. It is not loaded or linked by libspwkit.

local vspw = Proto("vspw", "Virtual SpaceWire Transport Protocol")

local MAGIC = 0x56535057 -- "VSPW"
local VERSION_MAJOR = 1
local VERSION_MINOR = 0
local HEADER_SIZE = 40
local MAX_FRAGMENT_PAYLOAD = 65467
local MAX_LOGICAL_PACKET = 16 * 1024 * 1024

local TYPE_DATA = 1
local TYPE_TIME_CODE = 2
local TYPE_LINK_CONTROL = 3
local TYPE_KEEPALIVE = 4
local TYPE_ACK = 5

local FLAG_EOP = 0x01
local FLAG_EEP = 0x02
local FLAG_FRAGMENT_START = 0x04
local FLAG_FRAGMENT_END = 0x08
local FLAG_ACK_REQUIRED = 0x10

local type_names = {
    [TYPE_DATA] = "DATA",
    [TYPE_TIME_CODE] = "TIME_CODE",
    [TYPE_LINK_CONTROL] = "LINK_CONTROL",
    [TYPE_KEEPALIVE] = "KEEPALIVE",
    [TYPE_ACK] = "ACK",
}

local f_magic = ProtoField.uint32("vspw.magic", "Magic", base.HEX)
local f_version_major = ProtoField.uint8("vspw.version_major", "Version major", base.DEC)
local f_version_minor = ProtoField.uint8("vspw.version_minor", "Version minor", base.DEC)
local f_type = ProtoField.uint8("vspw.type", "Message type", base.DEC, type_names)
local f_flags = ProtoField.uint8("vspw.flags", "Flags", base.HEX)
local f_eop = ProtoField.bool("vspw.flag.eop", "EOP")
local f_eep = ProtoField.bool("vspw.flag.eep", "EEP")
local f_fragment_start = ProtoField.bool("vspw.flag.fragment_start", "Fragment start")
local f_fragment_end = ProtoField.bool("vspw.flag.fragment_end", "Fragment end")
local f_ack_required = ProtoField.bool("vspw.flag.ack_required", "ACK required")
local f_header_size = ProtoField.uint16("vspw.header_size", "Header size", base.DEC)
local f_payload_size = ProtoField.uint16("vspw.payload_size", "Datagram payload size", base.DEC)
local f_link_id = ProtoField.uint32("vspw.link_id", "Virtual link ID", base.DEC_HEX)
local f_session_id = ProtoField.uint64("vspw.session_id", "Sender session ID", base.HEX)
local f_sequence = ProtoField.uint32("vspw.sequence", "Transport sequence", base.DEC)
local f_message_id = ProtoField.uint32("vspw.message_id", "Logical message ID", base.DEC)
local f_fragment_offset = ProtoField.uint32("vspw.fragment_offset", "Fragment offset", base.DEC)
local f_total_size = ProtoField.uint32("vspw.total_size", "Logical payload size", base.DEC)
local f_valid = ProtoField.bool("vspw.valid", "Structurally valid VSPW-TP v1 frame")
local f_data = ProtoField.bytes("vspw.data", "DATA fragment bytes")
local f_link_control = ProtoField.bytes("vspw.link_control", "LINK_CONTROL payload")
local f_time_count = ProtoField.uint8("vspw.time_count", "Time-code time count", base.DEC)
local f_time_control = ProtoField.uint8("vspw.time_control", "Time-code control flags", base.HEX)
local f_ack_session = ProtoField.uint64(
    "vspw.acknowledged_session_id", "Acknowledged sender session ID", base.HEX)

vspw.fields = {
    f_magic,
    f_version_major,
    f_version_minor,
    f_type,
    f_flags,
    f_eop,
    f_eep,
    f_fragment_start,
    f_fragment_end,
    f_ack_required,
    f_header_size,
    f_payload_size,
    f_link_id,
    f_session_id,
    f_sequence,
    f_message_id,
    f_fragment_offset,
    f_total_size,
    f_valid,
    f_data,
    f_link_control,
    f_time_count,
    f_time_control,
    f_ack_session,
}

local ex_malformed = ProtoExpert.new(
    "vspw.expert.malformed",
    "Malformed VSPW-TP frame",
    expert.group.MALFORMED,
    expert.severity.ERROR)
local ex_unsupported = ProtoExpert.new(
    "vspw.expert.unsupported",
    "Unsupported VSPW-TP version or message type",
    expert.group.PROTOCOL,
    expert.severity.WARN)
local ex_fragment = ProtoExpert.new(
    "vspw.expert.fragment",
    "VSPW-TP DATA fragment; correlate by sender session and logical message ID",
    expert.group.REASSEMBLE,
    expert.severity.NOTE)

vspw.experts = { ex_malformed, ex_unsupported, ex_fragment }

local function has_flag(flags, mask)
    return math.floor(flags / mask) % 2 == 1
end

local function known_type(message_type)
    return type_names[message_type] ~= nil
end

local function session_is_zero(tvb)
    return tvb(16, 4):uint() == 0 and tvb(20, 4):uint() == 0
end

local function session_text(tvb, offset)
    return string.format("%08x%08x", tvb(offset, 4):uint(), tvb(offset + 4, 4):uint())
end

local function validate_header(tvb)
    local length = tvb:len()
    if length < HEADER_SIZE then
        return false, "header shorter than 40 bytes", false
    end

    local version_major = tvb(4, 1):uint()
    local version_minor = tvb(5, 1):uint()
    local message_type = tvb(6, 1):uint()
    local flags = tvb(7, 1):uint()
    local header_size = tvb(8, 2):uint()
    local payload_size = tvb(10, 2):uint()
    local message_id = tvb(28, 4):uint()
    local fragment_offset = tvb(32, 4):uint()
    local total_size = tvb(36, 4):uint()

    if version_major ~= VERSION_MAJOR or version_minor > VERSION_MINOR then
        return false, string.format("unsupported VSPW-TP version %u.%u",
                                    version_major, version_minor), true
    end
    if not known_type(message_type) then
        return false, string.format("unsupported VSPW-TP message type %u", message_type), true
    end
    if header_size ~= HEADER_SIZE then
        return false, string.format("header_size=%u, expected %u", header_size, HEADER_SIZE), false
    end
    if payload_size > MAX_FRAGMENT_PAYLOAD then
        return false, string.format("payload_size=%u exceeds %u", payload_size,
                                    MAX_FRAGMENT_PAYLOAD), false
    end
    if length ~= HEADER_SIZE + payload_size then
        return false, string.format("UDP payload length=%u but header declares %u",
                                    length, HEADER_SIZE + payload_size), false
    end
    if session_is_zero(tvb) then
        return false, "sender session ID is zero", false
    end
    if flags >= 0x20 then
        return false, string.format("flags contain unknown bits: 0x%02x", flags), false
    end

    local eop = has_flag(flags, FLAG_EOP)
    local eep = has_flag(flags, FLAG_EEP)
    local start = has_flag(flags, FLAG_FRAGMENT_START)
    local finish = has_flag(flags, FLAG_FRAGMENT_END)
    local ack_required = has_flag(flags, FLAG_ACK_REQUIRED)

    if eop and eep then
        return false, "EOP and EEP are mutually exclusive", false
    end
    if message_type ~= TYPE_DATA and (eop or eep) then
        return false, "EOP/EEP flags are legal only on DATA", false
    end
    if ack_required and message_type ~= TYPE_DATA and message_type ~= TYPE_TIME_CODE then
        return false, "ACK_REQUIRED is legal only on DATA/TIME_CODE", false
    end

    if total_size > MAX_LOGICAL_PACKET then
        return false, string.format("logical payload size=%u exceeds %u", total_size,
                                    MAX_LOGICAL_PACKET), false
    end
    if fragment_offset > total_size or fragment_offset + payload_size > total_size then
        return false, "fragment range exceeds logical payload size", false
    end

    if message_type ~= TYPE_DATA then
        if fragment_offset ~= 0 or total_size ~= payload_size or start or finish then
            return false, "non-DATA frame carries DATA fragmentation metadata", false
        end
    else
        if total_size == payload_size then
            if fragment_offset ~= 0 or start or finish then
                return false, "unfragmented DATA has invalid fragment metadata", false
            end
        else
            if not start and not finish and payload_size == 0 then
                return false, "empty intermediate DATA fragment", false
            end
            if start and fragment_offset ~= 0 then
                return false, "FRAGMENT_START does not begin at offset zero", false
            end
            if finish and fragment_offset + payload_size ~= total_size then
                return false, "FRAGMENT_END does not end at total_size", false
            end
        end
    end

    if message_type == TYPE_TIME_CODE then
        if payload_size ~= 2 or total_size ~= 2 then
            return false, "TIME_CODE payload must be exactly two bytes", false
        end
    elseif message_type == TYPE_KEEPALIVE then
        if payload_size ~= 0 or total_size ~= 0 or message_id ~= 0 then
            return false, "KEEPALIVE must be header-only with message_id zero", false
        end
    elseif message_type == TYPE_ACK then
        if payload_size ~= 8 or total_size ~= 8 or message_id == 0 then
            return false, "ACK must carry an 8-byte session payload and non-zero message_id", false
        end
        if tvb(40, 4):uint() == 0 and tvb(44, 4):uint() == 0 then
            return false, "ACK acknowledged sender session ID is zero", false
        end
    end

    return true, nil, false
end

local function add_header_tree(root, tvb)
    local flags = tvb(7, 1):uint()

    root:add(f_magic, tvb(0, 4))
    root:add(f_version_major, tvb(4, 1))
    root:add(f_version_minor, tvb(5, 1))
    root:add(f_type, tvb(6, 1))

    local flags_tree = root:add(f_flags, tvb(7, 1))
    flags_tree:add(f_eop, has_flag(flags, FLAG_EOP))
    flags_tree:add(f_eep, has_flag(flags, FLAG_EEP))
    flags_tree:add(f_fragment_start, has_flag(flags, FLAG_FRAGMENT_START))
    flags_tree:add(f_fragment_end, has_flag(flags, FLAG_FRAGMENT_END))
    flags_tree:add(f_ack_required, has_flag(flags, FLAG_ACK_REQUIRED))

    root:add(f_header_size, tvb(8, 2))
    root:add(f_payload_size, tvb(10, 2))
    root:add(f_link_id, tvb(12, 4))
    root:add(f_session_id, tvb(16, 8))
    root:add(f_sequence, tvb(24, 4))
    root:add(f_message_id, tvb(28, 4))
    root:add(f_fragment_offset, tvb(32, 4))
    root:add(f_total_size, tvb(36, 4))
end

local function add_payload_tree(root, tvb, message_type, payload_size)
    if payload_size == 0 then
        return
    end

    if message_type == TYPE_TIME_CODE and payload_size == 2 then
        local item = root:add(tvb(40, 2), "TIME_CODE")
        item:add(f_time_count, tvb(40, 1))
        item:add(f_time_control, tvb(41, 1))
    elseif message_type == TYPE_ACK and payload_size == 8 then
        local item = root:add(tvb(40, 8), "ACK payload")
        item:add(f_ack_session, tvb(40, 8))
    elseif message_type == TYPE_DATA then
        root:add(f_data, tvb(40, payload_size))
    elseif message_type == TYPE_LINK_CONTROL then
        root:add(f_link_control, tvb(40, payload_size))
    end
end

local function set_info_column(tvb, pinfo)
    if tvb:len() < HEADER_SIZE then
        pinfo.cols.info = "VSPW-TP malformed short header"
        return
    end

    local message_type = tvb(6, 1):uint()
    local type_name = type_names[message_type] or string.format("TYPE_%u", message_type)
    local flags = tvb(7, 1):uint()
    local link_id = tvb(12, 4):uint()
    local sequence = tvb(24, 4):uint()
    local message_id = tvb(28, 4):uint()
    local offset = tvb(32, 4):uint()
    local total = tvb(36, 4):uint()
    local payload_size = tvb(10, 2):uint()

    local info = string.format("%s link=%u session=%s seq=%u",
                               type_name, link_id, session_text(tvb, 16), sequence)
    if message_id ~= 0 then
        info = info .. string.format(" msg=%u", message_id)
    end
    if message_type == TYPE_DATA then
        info = info .. string.format(" fragment=%u+%u/%u", offset, payload_size, total)
        if has_flag(flags, FLAG_EOP) then
            info = info .. " EOP"
        elseif has_flag(flags, FLAG_EEP) then
            info = info .. " EEP"
        end
    elseif message_type == TYPE_TIME_CODE and tvb:len() >= 42 then
        info = info .. string.format(" time=%u ctrl=0x%02x",
                                     tvb(40, 1):uint(), tvb(41, 1):uint())
    elseif message_type == TYPE_ACK and tvb:len() >= 48 then
        info = info .. " ack_session=" .. session_text(tvb, 40)
    end
    if has_flag(flags, FLAG_ACK_REQUIRED) then
        info = info .. " ACK_REQUIRED"
    end
    pinfo.cols.info = info
end

function vspw.dissector(tvb, pinfo, tree)
    local length = tvb:len()
    pinfo.cols.protocol = "VSPW-TP"

    local root = tree:add(vspw, tvb(0, length))
    if length < 4 then
        root:add(f_valid, false)
        root:add_proto_expert_info(ex_malformed, "frame is shorter than the VSPW magic")
        pinfo.cols.info = "VSPW-TP malformed"
        return length
    end

    root:add(f_magic, tvb(0, 4))
    if tvb(0, 4):uint() ~= MAGIC then
        root:add(f_valid, false)
        root:add_proto_expert_info(ex_malformed, "bad VSPW magic")
        pinfo.cols.info = "VSPW-TP malformed bad magic"
        return length
    end

    if length < HEADER_SIZE then
        root:add(f_valid, false)
        root:add_proto_expert_info(ex_malformed, "VSPW-TP header is shorter than 40 bytes")
        pinfo.cols.info = "VSPW-TP malformed short header"
        return length
    end

    -- Rebuild the tree from the complete fixed header so all filterable fields
    -- remain available even when later semantic validation fails.
    add_header_tree(root, tvb)

    local valid, reason, unsupported = validate_header(tvb)
    root:add(f_valid, valid)
    set_info_column(tvb, pinfo)

    if not valid then
        if unsupported then
            root:add_proto_expert_info(ex_unsupported, reason)
        else
            root:add_proto_expert_info(ex_malformed, reason)
        end
        return length
    end

    local message_type = tvb(6, 1):uint()
    local payload_size = tvb(10, 2):uint()
    add_payload_tree(root, tvb, message_type, payload_size)

    if message_type == TYPE_DATA and tvb(36, 4):uint() ~= payload_size then
        root:add_proto_expert_info(
            ex_fragment,
            string.format("DATA fragment: session=%s message_id=%u offset=%u size=%u total=%u",
                          session_text(tvb, 16), tvb(28, 4):uint(),
                          tvb(32, 4):uint(), payload_size, tvb(36, 4):uint()))
    end

    return length
end

local function heuristic_udp(tvb, pinfo, tree)
    if tvb:len() < 4 or tvb(0, 4):uint() ~= MAGIC then
        return false
    end
    vspw.dissector(tvb, pinfo, tree)
    return true
end

-- VSPW-TP UDP ports are runtime-configurable. Heuristic detection therefore
-- keys on the four-byte magic instead of hard-coding a port. Decode As remains
-- available when a capture contains intentionally malformed magic/headers.
vspw:register_heuristic("udp", heuristic_udp)
DissectorTable.get("udp.port"):add_for_decode_as(vspw)
