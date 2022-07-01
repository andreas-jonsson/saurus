#include <stdio.h>
#include <stdlib.h>

#include "node.h"
#include "parser.h"

extern FILE* yyin;

int main(int argc, char *argv[]) {
	parser_state state = {NULL, 0};

	if (argc > 1) {
		state.source_file = argv[1];
		yyin = fopen(argv[1], "r");
	}

	yyparse(&state);
	return EXIT_SUCCESS;
}
