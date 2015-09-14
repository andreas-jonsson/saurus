/*
 * Generated with Saurus v 0.0.1
 * http://www.saurus.org
 */

/* src/repl/saurus.su */

#include <saurus.h>

static int ___saurus(su_state *s);


#ifdef SU_OPT_C_LINE
	#line 9 "src/repl/saurus.su"
#endif

    #include <lua.h>
    #include <lualib.h>
    #include <lauxlib.h>

    #include <stdio.h>
    #include <string.h>
    #include <assert.h>



#ifdef SU_OPT_C_LINE
	#line 19 "src/repl/saurus.su"
#endif

    #define BUFFER_SIZE 512

    extern const char compiler_code[];
    char buffer[BUFFER_SIZE];
    su_state *compiler;

    const char *repl_help_text = "guru meditation...";

    int luaopen_writebin(lua_State *L);

    static const void *reader_fp(size_t *size, void *data) {
    	if (!size) return NULL;
    	*size = fread(buffer, 1, sizeof(buffer), (FILE*)data);
    	return (*size > 0) ? buffer : NULL;
    }



#ifdef SU_OPT_C_LINE
	#line 37 "src/repl/saurus.su"
#endif

    void shutdown() {
        su_close(compiler);
    }



#ifdef SU_OPT_C_LINE
	#line 43 "src/repl/saurus.su"
#endif

    int main(int argc, char *argv[]) {
    	int ret, i;
    	int compile;
    	FILE *fp;
    	jmp_buf err;
    	su_state *s;
    	lua_State *L;
    	char *tmp;
    	const char *tmp2;
    	const char *input, *output;
    	int print_help = argc > 1 && strstr("-h -v --help --version", argv[1]) != NULL;
    	int pipe = argc > 1 && !strcmp("--", argv[1]);

        compiler = su_init(NULL);
        su_libinit(compiler);
        ___saurus(compiler);
        atexit(shutdown);

    	if (!pipe && (argc <= 1 || print_help)) {
    		printf("S A U R U S\nCopyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>\nVersion: %s\n\n", su_version(NULL, NULL, NULL));
    		if (print_help) {
    			puts("Usage: saurus <options> <input.su> <output.suc>\n\tOptions:\n\t\t'-c' Compile source file to binary file.\n\t\t'--' read from STDIN.");
    			return 0;
    		}
    	}

    	s = su_init(NULL);
    	su_libinit(s);

    	L = lua_open();
    	luaL_openlibs(L);
    	luaopen_writebin(L);

    	if (luaL_loadstring(L, compiler_code)) {
    		fprintf(stderr, "%s\n", lua_tostring(L, -1));
    		lua_close(L);
    		su_close(s);
    		return -1;
    	}

    	if (lua_pcall(L, 0, 0, 0)) {
    		lua_getglobal(L, "saurus_error");
    		if (lua_isnil(L, -1))
    			lua_pop(L, 1);
    		fprintf(stderr, "%s\n", lua_tostring(L, -1));
    		lua_close(L);
    		su_close(s);
    		return -1;
    	}

    	ret = 0;
    	if (argc < 2 || pipe) {
    		if (!pipe)
    			puts("Type '_h' for help or '_q' to exit.\n");
    		su_pushstring(s, repl_help_text);
    		su_setglobal(s, "_h");
    		su_pushstring(s, "_q");
    		su_setglobal(s, "_q");

    		ret = setjmp(err);
    		if (!pipe)
    			su_seterror(s, err, ret);

    		jump: for (;;) {
    			if (!pipe)
    				printf("> ");
    			fgets(buffer, BUFFER_SIZE, stdin);

    			lua_getglobal(L, "repl");
    			lua_pushstring(L, buffer);
    			lua_pushstring(L, argv[0]);
    			if (lua_pcall(L, 2, 1, 0)) {
    				lua_getglobal(L, "saurus_error");
    				if (lua_isnil(L, -1)) {
    					lua_pop(L, 1);
    					puts(lua_tostring(L, -1));
    				} else {
    					puts(lua_tostring(L, -1));
    					lua_pop(L, 1);
    				}
    				lua_pop(L, 1);
    				goto jump;
    			}

    			lua_getglobal(L, "c_code");
    			su_assert(s, lua_isnil(L, -1), "Inline C is not supported in interpreter!");
    			lua_pop(L, 1);

    			tmp2 = lua_tolstring(L, -1, NULL);
    			su_assert(s, su_getglobal(s, "io"), "Could not retrieve 'io' namespace.");
    			su_pushstring(s, "print");
    			su_assert(s, su_map_get(s, -2), "Could not retrieve 'print' function.");

    			if (su_load(s, NULL, (void*)tmp2)) {
    				su_close(s);
    				lua_close(L);
    				fprintf(stderr, "Could not load: %s\n", argv[1]);
    				return -1;
    			}

    			lua_pop(L, 1);
    			su_call(s, 0, 1);
    			if (su_type(s, -1) == SU_STRING && !strcmp(su_tostring(s, -1, NULL), "_q")) {
    				ret = 0;
    				break;
    			}
    			su_call(s, 1, 0);
    			su_pop(s, 1);
    		}
    	} else {
    		compile = !strcmp(argv[1], "-c");
    		input = compile ? argv[2] : argv[1];

    		if (compile) {
    			if (argc < 4) {
    				fputs("Expected input and output file!", stderr);
    				lua_close(L);
    				su_close(s);
    				return -1;
    			}
    		} else {
    			fp = fopen(input, "rb");
    			if (!fp) {
    				fprintf(stderr, "Could not open: %s\n", input);
    				lua_close(L);
    				su_close(s);
    				return -1;
    			}

    			if (fread(buffer, 1, 1, fp) == 1) {
    				if (*buffer == '\x1b') {
    					rewind(fp);
    					if (su_load(s, &reader_fp, fp)) {
    						su_close(s);
    						lua_close(L);
    						fclose(fp);
    						fprintf(stderr, "Could not load: %s\n", input);
    						return -1;
    					}

    					su_pushstring(s, input);
    					for (i = 2; i < argc; i++)
                            su_pushstring(s, argv[i]);

                        su_call(s, argc - 1, 1);
                        if (su_type(s, -1) == SU_NUMBER)
                            ret = (int)su_tonumber(s, -1);

    					su_close(s);
    					lua_close(L);
    					fclose(fp);
    					return ret;
    				}
    			}
    			fclose(fp);
    		}

    		tmp = tmpnam(buffer);
    		output = compile ? argv[3] : tmp;

    		lua_getglobal(L, "compile");
    		lua_pushstring(L, input);
    		lua_pushstring(L, output);
    		lua_pushboolean(L, compile);
    		if (lua_pcall(L, 3, 0, 0)) {
    			lua_getglobal(L, "saurus_error");
    			if (lua_isnil(L, -1))
    				lua_pop(L, 1);
    			fprintf(stderr, "%s\n", lua_tostring(L, -1));
    			lua_close(L);
    			su_close(s);
    			return -1;
    		}

    		if (!compile) {
    			fp = fopen(output, "rb");
    			if (!fp) {
    				fprintf(stderr, "Could not open: %s\n", output);
    				lua_close(L);
    				su_close(s);
    				return -1;
    			}

    			if (su_load(s, &reader_fp, fp)) {
    				su_close(s);
    				lua_close(L);
    				fclose(fp);
    				remove(tmp);
    				fprintf(stderr, "Could not load: %s\n", output);
    				return -1;
    			}

    			fclose(fp);
    			remove(tmp);

    			su_pushstring(s, argv[1]);
    			for (i = 2; i < argc; i++)
    				su_pushstring(s, argv[i]);

                su_call(s, argc - 1, 1);
    			if (su_type(s, -1) == SU_NUMBER)
                    ret = (int)su_tonumber(s, -1);
    		}
    	}

    	lua_close(L);
    	su_close(s);
    	return ret;
    }



static int ___saurus(su_state *s) {
	const char code[] = {
		27,115,117,99,0,0,0,0,8,0,0,0,0,0,0,0,1,1,0,0,
		0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,
		21,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,18,0,0,
		0,115,114,99,47,114,101,112,108,47,115,97,117,114,117,115,46,115,117,8,
		0,0,0,10,0,0,0,10,0,0,0,20,0,0,0,20,0,0,0,38,
		0,0,0,38,0,0,0,44,0,0,0,1,0,0,0
	};


	if (su_load(s, NULL, (void*)code))
		return -1;
	return 0;
}
