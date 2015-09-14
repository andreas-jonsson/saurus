-- S A U R U S
-- Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
--
-- This Source Code Form is subject to the terms of the Mozilla Public
-- License, v. 2.0. If a copy of the MPL was not distributed with this
-- file, You can obtain one at http://mozilla.org/MPL/2.0/.

local opt_tail = true

local function table_clone(tab)
	local t = {}
	for k,v in ipairs(tab) do
		t[k] = v
	end
	return t
end

local function gen_inst(func, inst, line, ...)
	local instidx = #func.instructions + 1
	func.instructions[instidx] = {inst, ...}
	func.linenr[instidx] = line
end

local function call_or_tail(func, tail, line, narg)
	gen_inst(func, (tail and opt_tail and func.parent) and 'TCALL' or 'CALL', line, narg)
	func.stack_pointer = func.stack_pointer - narg
	assert(func.stack_pointer > 0)
end

local function gen_pop(func, num)
	num = num or 1
	local idx = #func.instructions
	local line = idx > 0 and func.linenr[idx] or -1
	gen_inst(func, 'POP', line, num)
	func.stack_pointer = func.stack_pointer - num
	assert(func.stack_pointer >= 0)
end

local function gen_call(func, ast, tail)
	assert(ast.type == 'call')
	gen(func, ast.func)
	for _,arg in ipairs(ast.args) do
		gen(func, arg)
	end
	call_or_tail(func, tail, ast.line, #ast.args)
end

local function find_lable(func, name)
	for i = #func.lables, 1, -1 do
		local v = func.lables[i]
		if v[1] == name then
			return v
		end
	end
end

local function find_up(func, name)
	local i = 0
	while func do
		local lable = find_lable(func, name)
		if lable then
			return i, lable[2]
		end
		i = i + 1
		func = func.parent
	end
end

local function push_const(func, value)
	for i,v in ipairs(func.constants) do
		if v == value then
			return i - 1
		end
	end
	table.insert(func.constants, value)
	return #func.constants - 1
end

local function gen_const(func, const)
	assert(const.type == 'number' or const.type == 'boolean' or const.type == 'string')
	local constid = push_const(func, const.value)
	gen_inst(func, 'PUSH', const.line, constid)
	func.stack_pointer = func.stack_pointer + 1
end

local function gen_fetch(func, ident)
	if func.parent and ident.value == 'rec' then
		gen_inst(func, 'LOAD', ident.line, 0)
	else
		local lv, id = find_up(func, ident.value)
		if not lv then
			gen_inst(func, 'GETGLOBAL', ident.line, push_const(func, ident.value))
		elseif lv == 0 then
			gen_inst(func, 'LOAD', ident.line, id + 1)
		else
			table.insert(func.up, {lv, id})
			gen_inst(func, 'LUP', ident.line, #func.up - 1)
		end
	end
	func.stack_pointer = func.stack_pointer + 1
end

local function gen_operator(func, ast, tail)
	assert(ast.type == 'operator')
	if ast.operator == '=' then
		assert(ast.left.type == 'identifier')
		table.insert(func.lables, {ast.left.value, func.stack_pointer})
		gen(func, ast.right, tail)
		gen_inst(func, 'LOAD', ast.line, func.stack_pointer)
		func.stack_pointer = func.stack_pointer + 1
		return
	elseif ast.operator == ':' or ast.operator == '..' then
		gen_inst(func, 'GETGLOBAL', ast.line, push_const(func, (ast.operator == '..') and 'range' or 'cons'))
		func.stack_pointer = func.stack_pointer + 1
		gen(func, ast.left)
		gen(func, ast.right)
		call_or_tail(func, tail, ast.line, 2)
		return
	elseif ast.operator == '.' then
		gen(func, ast.left)
		gen(func, ast.right)
		call_or_tail(func, tail, ast.line, 1)
		return
	elseif ast.operator == '>' then
		gen(func, ast.right)
		gen(func, ast.left)
		gen_inst(func, 'LESS', ast.line)
		func.stack_pointer = func.stack_pointer - 1
		return
	elseif ast.operator == '>=' then
		gen(func, ast.right)
		gen(func, ast.left)
		gen_inst(func, 'LEQUAL', ast.line)
		func.stack_pointer = func.stack_pointer - 1
		return
	end

	gen(func, ast.left)
	gen(func, ast.right)

	if ast.operator == '+' then
		gen_inst(func, 'ADD', ast.line)
	elseif ast.operator == '-' then
		gen_inst(func, 'SUB', ast.line)
	elseif ast.operator == '*' then
		gen_inst(func, 'MUL', ast.line)
	elseif ast.operator == '/' then
		gen_inst(func, 'DIV', ast.line)
	elseif ast.operator == '%' then
		gen_inst(func, 'MOD', ast.line)
	elseif ast.operator == '^' then
		gen_inst(func, 'POW', ast.line)
	elseif ast.operator == '==' then
		gen_inst(func, 'EQ', ast.line)
	elseif ast.operator == '~=' then
		gen_inst(func, 'EQ', ast.line)
		gen_inst(func, 'NOT', ast.line)
	elseif ast.operator == '&' then
		gen_inst(func, 'AND', ast.line)
	elseif ast.operator == '|' then
		gen_inst(func, 'OR', ast.line)
	elseif ast.operator == '<' then
		gen_inst(func, 'LESS', ast.line)
	elseif ast.operator == '<=' then
		gen_inst(func, 'LEQUAL', ast.line)
	else
		error('Invalid operator: ' .. ast.operator)
	end

	func.stack_pointer = func.stack_pointer - 1
	assert(func.stack_pointer > 0)
end

local function gen_dot(func, ast, tail)
	assert(ast.type == 'dot')
	gen(func, ast.left)
	for _,arg in ipairs(ast.path) do
		gen_const(func, arg)
		call_or_tail(func, false, ast.line, 1)
	end
end

local function gen_collection(func, ast, tail)
	assert(ast.type == 'collection')
	gen_inst(func, 'GETGLOBAL', ast.line, push_const(func, ast.collection))
	func.stack_pointer = func.stack_pointer + 1
	for _,arg in ipairs(ast.args) do
		gen(func, arg)
	end
	call_or_tail(func, tail, ast.line, #ast.args)
end

local function gen_block(func, ast, tail)
	assert(ast.type == 'block')
	local num = #ast.body
	local stack = func.stack_pointer
	local lables = table_clone(func.lables)

	for i = 1, num - 1 do
		gen(func, ast.body[i])
		gen_pop(func)
	end
	gen(func, ast.body[num], tail)
	local shift = func.stack_pointer - (stack + 1)
	if shift > 0 then
		gen_inst(func, 'SHIFT', ast.line, shift)
		func.stack_pointer = func.stack_pointer - shift
	end
	func.lables = lables
end

local function gen_cond(func, ast, tail)
	assert(ast.type == 'cond')
	local stack = func.stack_pointer
	gen(func, ast.cond)
	func.stack_pointer = stack

	local condidx = #func.instructions + 1
	func.instructions[condidx] = {'TEST', 0, 0}
	func.linenr[condidx] = ast.cond.line

	if ast._else then
		gen(func, ast._else, tail)
	else
		local n = {type = 'identifier', value = 'nil', file = ast._if.file, line = ast._if.line}
		gen(func, n, tail)
	end

	local jmpidx = #func.instructions + 1
	func.instructions[jmpidx] = {'JMP', 0, 0}
	func.linenr[jmpidx] = ast._else and ast._else.line or ast._if.line

	func.instructions[condidx][3] = #func.instructions

	func.stack_pointer = stack
	gen(func, ast._if, tail)
	func.instructions[jmpidx][3] = #func.instructions
	assert(#func.instructions == #func.linenr)
end

local function gen_for(func, ast, tail)
	assert(ast.type == 'for')

	local stack = func.stack_pointer + 1
	local bind_idx = #func.lables + 1
	local bind = {ast.name, stack}

	table.insert(func.lables, bind)
	gen(func, ast.value)

	local n = {type = 'identifier', value = 'nil', file = ast.file, line = ast.line}
	gen(func, n)

	local foridx = #func.instructions + 1
	func.instructions[foridx] = {'FOR', 0, 0}
	func.linenr[foridx] = ast.line

	gen(func, ast.body)

	gen_inst(func, 'SHIFT', ast.line, 1)
	func.stack_pointer = func.stack_pointer - 1

	local jmpidx = #func.instructions + 1
	func.instructions[jmpidx] = {'JMP', 0, foridx - 1}
	func.linenr[jmpidx] = ast.line

	func.instructions[foridx][3] = #func.instructions
	func.stack_pointer = stack

	assert(#func.instructions == #func.linenr)
	assert(table.remove(func.lables, bind_idx) == bind)
end

local function gen_lambda(parent, ast)
	assert(ast.type == 'lambda')
	local func = {
		instructions = {},
		linenr = {},
		lables = {},
		prototypes = {},
		constants = {},
		up = {},
		num_args = #ast.args,
		stack_pointer = 0,
		name = ast.file,
		parent = parent
	}

	table.insert(parent.prototypes, func)
	for i,arg in ipairs(ast.args) do
		table.insert(func.lables, {arg.value, i - 1})
	end
	func.stack_pointer = #ast.args
	gen_block(func, ast.body, true)
	gen_inst(func, 'RETURN', ast.body.line)
	assert(#func.instructions == #func.linenr)

	assert(not ast.varg or #ast.args == 1)
	gen_inst(parent, 'LAMBDA', ast.line, #parent.prototypes - 1, ast.varg and -1 or #ast.args)
	parent.stack_pointer = parent.stack_pointer + 1
end

function gen(func, ast, tail)
	if ast.type == 'identifier' then
		if ast.value == 'nil' then
			ast.type = 'string'
			ast.value = 'nil'
			gen_const(func, ast)
		else
			gen_fetch(func, ast)
		end
	elseif ast.type == 'lambda' then
		gen_lambda(func, ast)
	elseif ast.type == 'operator' then
		gen_operator(func, ast, tail)
	elseif ast.type == 'call' then
		gen_call(func, ast, tail)
	elseif ast.type == 'block' then
		gen_block(func, ast, tail)
	elseif ast.type == 'cond' then
		gen_cond(func, ast, tail)
	elseif ast.type == 'for' then
		gen_for(func, ast, tail)
	elseif ast.type == 'dot' then
		gen_dot(func, ast, tail)
	elseif ast.type == 'unm' then
		gen(func, ast.arg, tail)
		gen_inst(func, 'UNM', ast.line)
	elseif ast.type == 'not' then
		gen(func, ast.arg, tail)
		gen_inst(func, 'NOT', ast.line)
	elseif ast.type == 'unref' then
		gen_inst(func, 'GETGLOBAL', ast.line, push_const(func, 'unref'))
		func.stack_pointer = func.stack_pointer + 1
		gen(func, ast.arg)
		call_or_tail(func, tail, ast.line, 1)
	elseif ast.type == 'collection' then
		gen_collection(func, ast, tail)
	elseif ast.type == 'def' then
		gen(func, ast.value)
		gen_inst(func, 'SETGLOBAL', ast.line, push_const(func, ast.name))
	elseif ast.type == 'clambda' then
		gen_inst(func, 'LCL', ast.line, 0, ast.id)
		func.stack_pointer = func.stack_pointer + 1
	else
		gen_const(func, ast)
	end
end

function generate(ast, name)
	assert(ast.type == 'block')
	local func = {
		instructions = {},
		linenr = {},
		lables = {{'_ARGS', 0}},
		prototypes = {},
		constants = {},
		up = {},
		num_args = 1,
		stack_pointer = 1,
		name = name,
		parent = parent
	}
	gen_block(func, ast)
	gen_inst(func, 'RETURN', ast.line)
	assert(#func.instructions == #func.linenr)
	return func
end

function gen_print(asm, lv, out)
	local function puts(lv, out, ...)
		for i=1, lv do
			out:write('\t')
		end
		for _,v in ipairs({...}) do
			out:write(v .. '\t')
		end
		out:write('\n')
	end

	lv = (lv or -1) + 1
	out = out or io.output()
	puts(lv, out, '----------------------------')
	puts(lv, out, asm.name == '' and '?' or asm.name)
	puts(lv, out, '----------------------------')
	for i,v in ipairs(asm.instructions) do
		puts(lv, out, asm.linenr[i] .. ':', unpack(v))
		if v[1] == 'PUSH' or v[1] == 'GETGLOBAL' then
			puts(lv, out, '\t -> ' .. tostring(asm.constants[v[2] + 1]))
		end
	end
	for _,v in ipairs(asm.prototypes) do
		gen_print(v, lv, out)
	end
	puts(lv, out, '----------------------------')
end
