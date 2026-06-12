%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calcgrammar.tab.h"

int lineNumber = 1;
int exprNumber = 1;
%}

%%

"IF"|"if"              { return IF; }
"ELSE"|"else"          { return ELSE; }
"NOT"|"not"|"!"        { return NOT; }

"||"                   { return OR; }
"&&"                   { return AND; }

"<="                   { return LE; }
">="                   { return GE; }
"<"                    { return LT; }
">"                    { return GT; }

"+"                    { return PLUS; }
"-"                    { return MINUS; }
"*"                    { return MULT; }
"/"                    { return DIV; }
"%"                    { return MOD; }
"^"                    { return POWER; }

"("                    { return LPAREN; }
")"                    { return RPAREN; }
"{"                    { return LBRACE; }
"}"                    { return RBRACE; }

";"                    { exprNumber++; return SEMI; }

[0-9]+(\.[0-9]+)?      { yylval.u_double = atof(yytext); return VAL; }

[ \t\r]+               { }

\n+                    { lineNumber += yyleng; exprNumber++; return DONE; }

.                      { printf("Error: Invalid token '%s' at line %d\n", yytext, lineNumber); return 0; }

%%

int yywrap(void)
{
    return 1;
}
