-- S A U R U S
-- Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
--
-- This Source Code Form is subject to the terms of the Mozilla Public
-- License, v. 2.0. If a copy of the MPL was not distributed with this
-- file, You can obtain one at http://mozilla.org/MPL/2.0/.

require('parser')
require('generate')
require('assemble')

SAURUS_VERSION = {0, 0, 1}
SAURUS_VERSION_STRING = string.format('%i.%i.%i', unpack(SAURUS_VERSION))

function get_file_path(str)
	local root_path = str
	if str then
		local index = str:reverse():find('/', 1, true)
		root_path = index and str:sub(1, -index) or '.'
	end
	return root_path
end

function repl(src, name, path, asm_out)
	c_guard = {}
	c_func = {}
	c_code = nil

	local stream = create_stream(src, name or '?', path or '')
	local ast = read_block(stream)
	if not ast then
		saurus_error = 'No code generated!'
		error(saurus_error)
	end

	--ast_print(ast)

	local asm = generate(ast, name or '?')
	if asm_out then
		gen_print(asm, nil, asm_out)
		return
	end
	return assemble(asm, dest)
end

function compile(src, dest, compile)
	local fp = io.open(src, 'rb')
	if not fp then
		saurus_error = 'Could not open: ' .. src
		error(saurus_error)
	end

	local out_fp = io.open(dest, 'wb')
	if not fp then
		saurus_error = 'Could not open: ' .. dest
		error(saurus_error)
	end

	local asm_out = compile and string.sub(dest, -4) == '.sua'
	local res = repl(fp:read('*a'), src, get_file_path(src), asm_out and out_fp)

	fp:close()
	fp = out_fp

	if compile and string.sub(dest, -2) == '.c' then
		fp:write(string.format('/*\n * Generated with Saurus v %s\n', SAURUS_VERSION_STRING))
		fp:write(string.format(' * http://www.saurus.org\n */\n\n', src))
		fp:write(string.format('/* %s */\n\n#include <saurus.h>\n\n', src))

		if c_code then
			fp:write('static int ___saurus(su_state *s);\n\n')
			fp:write(string.format('%s\n', c_code))
		end

		fp:write([[
static int ___saurus(su_state *s) {
	const char code[] = {]])

		fp:write('\n\t\t')
		local len = #res
		for i = 1, len - 1 do
			fp:write(string.format('%i,', string.byte(string.sub(res, i, i))))
			if i % 20 == 0 then
				fp:write('\n\t\t')
			end
		end

		fp:write(string.format('%i\n', string.byte(string.sub(res, len, len))))
		fp:write('\t};\n\n')

		for _,f in ipairs(c_func) do
			if f.enum then
				fp:write(string.format('\tsu_pushnumber(s, (double)%s);\n', f.enum))
				fp:write('\tsu_clambda(s, NULL);\n')
			else
				fp:write(string.format('\tsu_clambda(s, &___%s);\n', f.name))
			end
		end

		fp:write([[

	if (su_load(s, NULL, (void*)code))
		return -1;
	return 0;
}
]])

	elseif asm_out then
		-- Do nothing.
	else
		if c_code then
			saurus_error = '.suc does not support C inline code'
			error(saurus_error)
		end
		fp:write(res)
	end
	fp:close()
end
