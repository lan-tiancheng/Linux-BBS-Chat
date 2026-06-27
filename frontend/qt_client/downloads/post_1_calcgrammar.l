%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calcgrammar.tab.h"

int lineNumber = 1;
int exprNumber = 1;
%}

%%

"+"                     { return PLUS; }
"-"                     { return MINUS; }
"*"                     { return MULT; }
"/"                     { return DIV; }
"%"                     { return MOD; }
"^"                     { return POWER; }
"="                     { return ASSIGN; }
"("                     { return LBRAC; }
")"                     { return RBRAC; }
";"                     { exprNumber++; return SEMI; }
"\n"                    { lineNumber++; return DONE; }

[0-9]+"."[0-9]+        { yylval.u_float = atof(yytext); return VALf; }
[0-9]+                  { yylval.u_int = atoi(yytext); return VALi; }
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.u_str = strdup(yytext); return ID; }

[ \t\r]+                { /* ignore whitespace */ }
.                       { printf("Error: Invalid token %s\n", yytext); }

%%

int yywrap(void)
{
    return 1;
}