for _,arg in ipairs(_ARGS) do
    if     arg == 'debug'      then debug_compiler = true
    elseif arg == 'html'       then html = true
    elseif arg == 'm'          then m = true
    elseif arg == 'dl'         then dl = true
    elseif arg == 'no-pattern' then no_pattern = true
    elseif arg == 'no-dynlib'  then no_dynlib = true
    elseif arg == 'no-io'      then no_io = true
    elseif arg == 'generate'   then generate = true
    elseif arg == 'no-socket'  then no_socket = true
    elseif arg == 'winsock'    then winsock = true
    elseif arg == 'pthread'    then pthread = true
    else error('Unknown argument: ' .. arg) end
end

function create_project(k)
    kind(k)
    language 'C'
    targetdir ''
    platforms { 'native', 'x32', 'x64' }
    includedirs { 'lua', 'src/vm' }
    defines { '_CRT_SECURE_NO_WARNINGS' }

    if k == 'ConsoleApp' then
        files {
            'src/repl/saurus.c'
        }
        links { 'libsaurus' }

        if html then targetextension '.html' end
    else
        files {
            'src/vm/*.h', 'src/vm/*.c',
            'src/compiler/*.c',
            'lua/**.h', 'lua/**.c'
        }
        targetname 'saurus'
    end

    if m then links { 'm' } end
    if dl then links { 'dl' } end
    if winsock then links { 'WS2_32' } end
    if pthread then links { 'pthread' } end
    if no_pattern then defines { 'SU_OPT_NO_PATTERN' } end
    if no_socket then defines { 'SU_OPT_NO_SOCKET' } end

    if os.getenv('SU_OPT_NO_FILE_IO') or no_io then defines { 'SU_OPT_NO_FILE_IO' } end
    if os.getenv('SU_OPT_NO_DYNLIB') or no_dynlib then defines { 'SU_OPT_NO_DYNLIB' } end
end

if generate then
    require 'pack'
    assert(os.execute('saurus -c src/repl/saurus.su src/repl/saurus.c') == 0)
    assert(os.execute('saurus -c src/compiler/compiler.su src/compiler/compiler.c') == 0)
end

solution 'saurus'
   configurations { 'Debug', 'Release' }

   configuration 'Debug'
      defines { 'DEBUG' }
      flags { 'Symbols' }

   configuration 'Release'
      defines { 'RELEASE' }
      flags { 'Optimize' }

   project 'saurus'
      create_project 'ConsoleApp'

   project 'libsaurus'
      create_project 'StaticLib'
