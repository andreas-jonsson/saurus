print('Generating compiler...')

local entry_script = assert(io.open('./src/compiler/entry.lua', 'r')):read('*a')
local new_script = string.gsub(entry_script, 'require%(\'([%w_]+)\'%)', function(s) return
	'\ndo\n' .. assert(io.open('./src/compiler/' .. s .. '.lua', 'r')):read('*a') .. '\nend\n'
end)

local output = 'const char compiler_code[] = {'
for i = 1, #new_script do
	local ch = string.sub(new_script, i, i)
	assert(#ch == 1)
	if (i - 1) % 16 == 0 then
		output = output .. '\n\t'
	end
	output = string.format('%s0x%x, ', output, string.byte(ch))
end
output = output .. '0x0};\n'

assert(io.open('./src/compiler/lua.c', 'w')):write(output)

if debug_compiler then
	print('Writing debug information...')
	assert(io.open('./_dbg.lua', 'w')):write(new_script)
end
