-- S A U R U S
-- Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
--
-- This Source Code Form is subject to the terms of the Mozilla Public
-- License, v. 2.0. If a copy of the MPL was not distributed with this
-- file, You can obtain one at http://mozilla.org/MPL/2.0/.

local instruction_set = {
	'PUSH',
	'POP',
	'LOAD',
	'LUP',
	'LCL',

	'ADD',
	'SUB',
	'MUL',
	'DIV',
	'MOD',
	'POW',
	'UNM',

	'EQ',
	'LESS',
	'LEQUAL',

	'NOT',
	'AND',
	'OR',

	'TEST',
	'FOR',
	'JMP',

	'RETURN',
	'SHIFT',
	'CALL',
	'TCALL',
	'LAMBDA',

	'GETGLOBAL',
	'SETGLOBAL'
}

local instruction_matrix = {}
for i,v in ipairs(instruction_set) do
	assert(not instruction_matrix[v])
	instruction_matrix[v] = i - 1
end

local function asm_error(file, line, ...)
	saurus_error = string.format(...) .. string.format(' error: %s:%i', file, line)
	--print(debug.traceback())
	error(saurus_error)
end

function asm_func(asm, bin)
	bin:write(writebin.uint32(#asm.instructions))
	for i,v in ipairs(asm.instructions) do
		local inst = assert(instruction_matrix[v[1]], 'Invalid instruction: ' .. v[1])
		v[2] = v[2] or 0
		v[3] = v[3] or 0

		if v[2] > 255 or v[3] > (2 ^ 15) then
			asm_error(asm.name, asm.linenr[i], "out of register space (i=%s a=%i, b=%i)", v[1], v[2], v[3])
		end

		bin:write(writebin.uint8(inst))
		bin:write(writebin.uint8(v[2] or 0))
		bin:write(writebin.int16(v[3] or 0))
	end

	bin:write(writebin.uint32(#asm.constants))
	for _,v in ipairs(asm.constants) do
		local t = type(v)
		if t == 'string' then
			if v == 'nil' then
				bin:write(writebin.uint8(0))
			else
				bin:write(writebin.uint8(4))
				bin:write(writebin.string(v))
			end
		elseif t == 'number' then
			bin:write(writebin.uint8(3))
			bin:write(writebin.number(v))
		elseif t == 'boolean' then
			bin:write(writebin.uint8(v and 2 or 1))
		else
			error('Invalid type!')
		end
	end

	bin:write(writebin.uint32(#asm.up))
	for _,v in ipairs(asm.up) do
		bin:write(writebin.uint16(v[1]))
		bin:write(writebin.uint16(v[2]))
	end

	bin:write(writebin.uint32(#asm.prototypes))
	for _,v in ipairs(asm.prototypes) do
		asm_func(v, bin)
	end

	bin:write(writebin.string(asm.name))
	bin:write(writebin.uint32(#asm.linenr))
	for _,v in ipairs(asm.linenr) do
		bin:write(writebin.uint32(v))
	end
end

function assemble(asm, dest)
	local bin = {
		data = '',
		write = function(self, str)
			self.data = self.data .. str
		end
	}

	bin:write(writebin.header(SAURUS_VERSION[1], SAURUS_VERSION[2]))
	asm_func(asm, bin)
	return bin.data
end
