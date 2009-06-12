#define _GNU_SOURCE
#include "ply-scan.h"
#include "ply-hashtable.h"
#include "ply-list.h"
#include "ply-bitarray.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdbool.h>

#include "script.h"
#include "script-parse.h"


#define WITH_SEMIES




/*                          done    todo
int var (exp)           tm   =      ! ++ -- 
f() f[] f.a             pi   =      
* / %                   md   =
+ -                     pm   =      && || 
< <= > >=               gt   =
== !=                   eq   =
=                       as   =      += -= *= %=

*/


static script_op* script_parse_op (ply_scan_t* scan);
static script_exp* script_parse_exp (ply_scan_t* scan);
static ply_list_t* script_parse_op_list (ply_scan_t* scan);
static void script_parse_op_list_free (ply_list_t* op_list);

static script_exp* script_parse_exp_tm (ply_scan_t* scan)
{
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 script_exp* exp = NULL;
 if (curtoken->type == PLY_SCAN_TOKEN_TYPE_INTEGER){
    exp = malloc(sizeof(script_exp));
    exp->type = SCRIPT_EXP_TYPE_TERM_INT;
    exp->data.integer = curtoken->data.integer;
    ply_scan_get_next_token(scan);
    return exp;
    }
 
 if (curtoken->type == PLY_SCAN_TOKEN_TYPE_IDENTIFIER){
    exp = malloc(sizeof(script_exp));
    if (!strcmp(curtoken->data.string, "NULL")){
        exp->type = SCRIPT_EXP_TYPE_TERM_NULL;
        }
    else if (!strcmp(curtoken->data.string, "global")){
        exp->type = SCRIPT_EXP_TYPE_TERM_GLOBAL;
        }
    else if (!strcmp(curtoken->data.string, "local")){
        exp->type = SCRIPT_EXP_TYPE_TERM_LOCAL;
        }
    else {
        exp->type = SCRIPT_EXP_TYPE_TERM_VAR;
        exp->data.string = strdup(curtoken->data.string);
        }
    curtoken = ply_scan_get_next_token(scan);
    return exp;
    }
 
 if (curtoken->type == PLY_SCAN_TOKEN_TYPE_STRING){
    exp = malloc(sizeof(script_exp));
    exp->type = SCRIPT_EXP_TYPE_TERM_STRING;
    exp->data.string = strdup(curtoken->data.string);
    ply_scan_get_next_token(scan);
    return exp;
    }
 
 if (curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL && curtoken->data.symbol == '('){
    ply_scan_get_next_token(scan);
    exp = script_parse_exp (scan);
    assert(exp);                                        //FIXME syntax error
    curtoken = ply_scan_get_current_token(scan);
    assert(curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL && curtoken->data.symbol == ')');
    ply_scan_get_next_token(scan);
    return exp;
    }
 return exp;
}

static script_exp* script_parse_exp_pi (ply_scan_t* scan)
{
 script_exp* exp = script_parse_exp_tm (scan);
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 while (true){
    if (curtoken->type != PLY_SCAN_TOKEN_TYPE_SYMBOL) break;
    if (curtoken->data.symbol == '('){
        script_exp* func = malloc(sizeof(script_exp));
        ply_list_t* parameters = ply_list_new();
        ply_scan_get_next_token(scan);
        while (true){
            if (curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL && curtoken->data.symbol == ')') break;

            script_exp* parameter = script_parse_exp (scan);

            ply_list_append_data (parameters, parameter);

            curtoken = ply_scan_get_current_token(scan);
            assert(curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL);       //FIXME syntax error
            if (curtoken->data.symbol == ')') break;
            assert (curtoken->data.symbol == ',');                      //FIXME syntax error
            ply_scan_get_next_token(scan);
            }
        ply_scan_get_next_token(scan);
        func->data.function.name = exp;
        exp = func;
        exp->type = SCRIPT_EXP_TYPE_FUNCTION;
        exp->data.function.parameters = parameters;
        continue;
        }

    script_exp* key;

    if (curtoken->data.symbol == '.'){
        ply_scan_get_next_token(scan);
        if (curtoken->type == PLY_SCAN_TOKEN_TYPE_IDENTIFIER){
            key = malloc(sizeof(script_exp));
            key->type = SCRIPT_EXP_TYPE_TERM_STRING;
            key->data.string = strdup(curtoken->data.string);
            }
        else if (curtoken->type == PLY_SCAN_TOKEN_TYPE_INTEGER){        // errrr, integer keys without being [] bracketted
            key = malloc(sizeof(script_exp));                           // This is broken with floats as obj.10.6 is obj[10.6] and not obj[10][6]
            key->type = SCRIPT_EXP_TYPE_TERM_INT;
            key->data.integer = curtoken->data.integer;
            }
        else assert(0);                                                 //FIXME syntax error
        curtoken = ply_scan_get_next_token(scan);
        }
    else if (curtoken->data.symbol == '['){
        ply_scan_get_next_token(scan);
        key = script_parse_exp (scan);
        curtoken = ply_scan_get_current_token(scan);
        assert (curtoken->data.symbol == ']');
        curtoken = ply_scan_get_next_token(scan);
        }
    else break;
    script_exp* hash = malloc(sizeof(script_exp));
    hash->type = SCRIPT_EXP_TYPE_HASH;
    hash->data.dual.sub_a = exp;
    hash->data.dual.sub_b = key;
    exp = hash;                                         // common hash lookup bit;


    }
 return exp;
}




static script_exp* script_parse_exp_md (ply_scan_t* scan)
{
 script_exp* sub_a = script_parse_exp_pi (scan);
 if (!sub_a) return NULL;
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 while (curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL &&
            (curtoken->data.symbol == '*' ||
             curtoken->data.symbol == '/' ||
             curtoken->data.symbol == '%' )){
    script_exp* exp = malloc(sizeof(script_exp));
    if (curtoken->data.symbol == '*')       exp->type = SCRIPT_EXP_TYPE_MUL;
    else if (curtoken->data.symbol == '/')  exp->type = SCRIPT_EXP_TYPE_DIV;
    else                                    exp->type = SCRIPT_EXP_TYPE_MOD;
    exp->data.dual.sub_a = sub_a;
    ply_scan_get_next_token(scan);
    exp->data.dual.sub_b = script_parse_exp_pi (scan);
    assert(exp->data.dual.sub_b);                                        //FIXME syntax error
    sub_a = exp;
    curtoken = ply_scan_get_current_token(scan);
    }
 
 return sub_a;
}

static script_exp* script_parse_exp_pm (ply_scan_t* scan)
{
 script_exp* sub_a = script_parse_exp_md (scan);
 if (!sub_a) return NULL; 
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 while (curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL &&
            (curtoken->data.symbol == '+' ||
             curtoken->data.symbol == '-' )){
    script_exp* exp = malloc(sizeof(script_exp));
    if (curtoken->data.symbol == '+')   exp->type = SCRIPT_EXP_TYPE_PLUS;
    else                                exp->type = SCRIPT_EXP_TYPE_MINUS;
    exp->data.dual.sub_a = sub_a;
    ply_scan_get_next_token(scan);
    exp->data.dual.sub_b = script_parse_exp_md (scan);
    assert(exp->data.dual.sub_b);                                        //FIXME syntax error
    sub_a = exp;
    curtoken = ply_scan_get_current_token(scan);
    }
 
 return sub_a;
}

static script_exp* script_parse_exp_gt (ply_scan_t* scan)
{
 script_exp* sub_a = script_parse_exp_pm (scan);
 if (!sub_a) return NULL; 
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 ply_scan_token_t* peektoken = ply_scan_peek_next_token(scan);
 while (curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL &&
            (curtoken->data.symbol == '<' ||
             curtoken->data.symbol == '>' )){                           // FIXME make sure we dont consume <<= or >>=
    int gt = (curtoken->data.symbol == '>');
    int eq = 0;
    curtoken = ply_scan_get_next_token(scan);
    if (curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL && 
             curtoken->data.symbol == '='){
        eq = 1;
        curtoken = ply_scan_get_next_token(scan);
        }
    script_exp* exp = malloc(sizeof(script_exp));
    if      ( gt &&  eq) exp->type = SCRIPT_EXP_TYPE_GE;
    else if ( gt && !eq) exp->type = SCRIPT_EXP_TYPE_GT;
    else if (!gt &&  eq) exp->type = SCRIPT_EXP_TYPE_LE;
    else                 exp->type = SCRIPT_EXP_TYPE_LT;
    
    exp->data.dual.sub_a = sub_a;
    exp->data.dual.sub_b = script_parse_exp_pm (scan);
    assert(exp->data.dual.sub_b);                                        //FIXME syntax error
    sub_a = exp;
    curtoken = ply_scan_get_current_token(scan);
    peektoken = ply_scan_peek_next_token(scan);
    }
 
 return sub_a;
}

static script_exp* script_parse_exp_eq (ply_scan_t* scan)
{
 script_exp* sub_a = script_parse_exp_gt (scan);
 if (!sub_a) return NULL; 
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 ply_scan_token_t* peektoken = ply_scan_peek_next_token(scan);
 while (1){
    if (curtoken->type != PLY_SCAN_TOKEN_TYPE_SYMBOL) break;
    if (peektoken->type != PLY_SCAN_TOKEN_TYPE_SYMBOL) break;
    
    if (peektoken->data.symbol != '=') break;
    if ((curtoken->data.symbol != '=') && (curtoken->data.symbol != '!')) break;
    int ne = (curtoken->data.symbol == '!');
    ply_scan_get_next_token(scan);
    ply_scan_get_next_token(scan);
    
    script_exp* exp = malloc(sizeof(script_exp));
    if (ne)   exp->type = SCRIPT_EXP_TYPE_NE;
    else      exp->type = SCRIPT_EXP_TYPE_EQ;
    exp->data.dual.sub_a = sub_a;
    exp->data.dual.sub_b = script_parse_exp_gt (scan);
    assert(exp->data.dual.sub_b);                                        //FIXME syntax error
    sub_a = exp;
    curtoken = ply_scan_get_current_token(scan);
    peektoken = ply_scan_peek_next_token(scan);
    }
 
 return sub_a;
}

static script_exp* script_parse_exp_as (ply_scan_t* scan)
{
 script_exp* lhs = script_parse_exp_eq (scan);
 if (!lhs) return NULL; 
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 
 if (curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL && curtoken->data.symbol == '=' ){
    ply_scan_get_next_token(scan);
    script_exp* rhs = script_parse_exp_eq(scan);
    assert(rhs);                                        //FIXME syntax error
    script_exp* exp = malloc(sizeof(script_exp));
    exp->type = SCRIPT_EXP_TYPE_ASSIGN;
    exp->data.dual.sub_a = lhs;
    exp->data.dual.sub_b = rhs;
    return exp;
    }
 return lhs;
}

static script_exp* script_parse_exp (ply_scan_t* scan)
{
 return script_parse_exp_as (scan);
 
}

static script_op* script_parse_op_block (ply_scan_t* scan)
{
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 
 if (curtoken->type != PLY_SCAN_TOKEN_TYPE_SYMBOL  || curtoken->data.symbol != '{' )
    return NULL;
 
 ply_scan_get_next_token(scan);
 ply_list_t* sublist = script_parse_op_list (scan);
 assert(curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL);       //FIXME syntax error
 assert(curtoken->data.symbol == '}');
 curtoken = ply_scan_get_next_token(scan);

 script_op* op = malloc(sizeof(script_op));
 op->type = SCRIPT_OP_TYPE_OP_BLOCK;
 op->data.list = sublist;
 return op;
}

static script_op* script_parse_if_while (ply_scan_t* scan)
{
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 script_op_type type;
 if (curtoken->type != PLY_SCAN_TOKEN_TYPE_IDENTIFIER)
    return NULL;
 if       (!strcmp(curtoken->data.string, "if"))    type = SCRIPT_OP_TYPE_IF;
 else if  (!strcmp(curtoken->data.string, "while")) type = SCRIPT_OP_TYPE_WHILE;
 else return NULL;
 
 curtoken = ply_scan_get_next_token(scan);
 assert(curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL);       //FIXME syntax error
 assert(curtoken->data.symbol == '(');
 curtoken = ply_scan_get_next_token(scan);
 
 script_exp* cond = script_parse_exp (scan);
 assert(cond);                                               //FIXME syntax error
 
 curtoken = ply_scan_get_current_token(scan);
 assert(curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL);       //FIXME syntax error
 assert(curtoken->data.symbol == ')');
 curtoken = ply_scan_get_next_token(scan);
 
 script_op* cond_op = script_parse_op(scan);
 
 script_op* op = malloc(sizeof(script_op));
 op->type = type;
 op->data.cond_op.cond = cond;
 op->data.cond_op.op = cond_op;
 return op;
}





static script_op* script_parse_function (ply_scan_t* scan)
{
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 
 ply_list_t *parameter_list;
 
 if (curtoken->type != PLY_SCAN_TOKEN_TYPE_IDENTIFIER)
    return NULL;
 if (strcmp(curtoken->data.string, "fun")) return NULL;
 
 curtoken = ply_scan_get_next_token(scan);
 assert(curtoken->type == PLY_SCAN_TOKEN_TYPE_IDENTIFIER);       //FIXME syntax error
 
 script_exp* name = malloc(sizeof(script_exp));
 name->type = SCRIPT_EXP_TYPE_TERM_VAR;
 name->data.string = strdup(curtoken->data.string);

 curtoken = ply_scan_get_next_token(scan);
 
 assert(curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL);       //FIXME syntax error
 assert(curtoken->data.symbol == '(');
 curtoken = ply_scan_get_next_token(scan);
 parameter_list = ply_list_new();
 
 
 while (true){
    if (curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL && curtoken->data.symbol == ')') break;
    assert (curtoken->type == PLY_SCAN_TOKEN_TYPE_IDENTIFIER);
    char* parameter = strdup(curtoken->data.string);
    ply_list_append_data (parameter_list, parameter);
    
    curtoken = ply_scan_get_next_token(scan);
    assert(curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL);       //FIXME syntax error
    if (curtoken->data.symbol == ')') break;
    assert (curtoken->data.symbol == ',');                      //FIXME syntax error
    ply_scan_get_next_token(scan);
    }
 
 curtoken = ply_scan_get_next_token(scan);
 
 script_op* func_op = script_parse_op(scan);
 

 
 script_op* op = malloc(sizeof(script_op));
 op->type = SCRIPT_OP_TYPE_FUNCTION_DEF;
 op->data.function_def.name = name;
 op->data.function_def.function = script_function_script_new(func_op, NULL, parameter_list);
 return op;
}




static script_op* script_parse_return (ply_scan_t* scan)
{
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 if (curtoken->type != PLY_SCAN_TOKEN_TYPE_IDENTIFIER)
    return NULL;
 script_op_type type;
 if      (!strcmp(curtoken->data.string, "return"))   type = SCRIPT_OP_TYPE_RETURN;
 else if (!strcmp(curtoken->data.string, "break"))    type = SCRIPT_OP_TYPE_BREAK;
 else if (!strcmp(curtoken->data.string, "continue")) type = SCRIPT_OP_TYPE_CONTINUE;
 else return NULL;
 
 curtoken = ply_scan_get_next_token(scan);
 
 script_op* op = malloc(sizeof(script_op));
 if (type == SCRIPT_OP_TYPE_RETURN){
    op->data.exp = script_parse_exp (scan);                    // May be NULL
    curtoken = ply_scan_get_current_token(scan);
    }

#ifdef WITH_SEMIES
    assert(curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL);
    assert(curtoken->data.symbol == ';');
    curtoken = ply_scan_get_next_token(scan);
#endif

 op->type = type;
 return op;
}








static script_op* script_parse_op (ply_scan_t* scan)
{
 ply_scan_token_t* curtoken = ply_scan_get_current_token(scan);
 script_op* reply = NULL;
 
 
 reply = script_parse_op_block (scan);
 if (reply) return reply;
 
 reply = script_parse_if_while (scan);
 if (reply) return reply;

 reply = script_parse_return (scan);
 if (reply) return reply;

 reply = script_parse_function (scan);
 if (reply) return reply;

// if curtoken->data.string == "if/for/while...


// default is expression
    {
    script_exp* exp = script_parse_exp(scan);
    if (!exp) return NULL;
    curtoken = ply_scan_get_current_token(scan);
#ifdef WITH_SEMIES
    assert(curtoken->type == PLY_SCAN_TOKEN_TYPE_SYMBOL);
    assert(curtoken->data.symbol == ';');
    curtoken = ply_scan_get_next_token(scan);
#endif

    script_op* op = malloc(sizeof(script_op));
    op->type = SCRIPT_OP_TYPE_EXPRESSION;
    op->data.exp = exp;
    return op;
    }
 return NULL;
}

static ply_list_t* script_parse_op_list (ply_scan_t* scan)
{
 ply_list_t *op_list = ply_list_new();
 
 while(1){
    script_op* op = script_parse_op (scan);
    if (!op) break;
    ply_list_append_data (op_list, op);
    }
 
 return op_list;
}

static void script_parse_exp_free (script_exp* exp)
{
 if (!exp) return;
 switch (exp->type){
    case SCRIPT_EXP_TYPE_PLUS:
    case SCRIPT_EXP_TYPE_MINUS:
    case SCRIPT_EXP_TYPE_MUL:
    case SCRIPT_EXP_TYPE_DIV:
    case SCRIPT_EXP_TYPE_MOD:
    case SCRIPT_EXP_TYPE_EQ:
    case SCRIPT_EXP_TYPE_NE:
    case SCRIPT_EXP_TYPE_GT:
    case SCRIPT_EXP_TYPE_GE:
    case SCRIPT_EXP_TYPE_LT:
    case SCRIPT_EXP_TYPE_LE:
    case SCRIPT_EXP_TYPE_ASSIGN:
    case SCRIPT_EXP_TYPE_HASH:
        script_parse_exp_free (exp->data.dual.sub_a);
        script_parse_exp_free (exp->data.dual.sub_b);
        break;
    case SCRIPT_EXP_TYPE_TERM_INT:
    case SCRIPT_EXP_TYPE_TERM_NULL:
    case SCRIPT_EXP_TYPE_TERM_LOCAL:
    case SCRIPT_EXP_TYPE_TERM_GLOBAL:
        break;
    case SCRIPT_EXP_TYPE_FUNCTION:
        {
        ply_list_node_t *node;
        for (node = ply_list_get_first_node (exp->data.function.parameters);
             node;
             node = ply_list_get_next_node (exp->data.function.parameters, node)){
            script_exp* sub = ply_list_node_get_data (node);
            script_parse_exp_free (sub);
            }
        ply_list_free(exp->data.function.parameters);
        script_parse_exp_free (exp->data.function.name);
        break;
        }
    case SCRIPT_EXP_TYPE_TERM_STRING:
    case SCRIPT_EXP_TYPE_TERM_VAR:
        free (exp->data.string);
        break;
    }
 free(exp);
}



void script_parse_op_free (script_op* op)
{
 switch (op->type){
    case SCRIPT_OP_TYPE_EXPRESSION:
        {
        script_parse_exp_free (op->data.exp);
        break;
        }
    case SCRIPT_OP_TYPE_OP_BLOCK:
        {
        script_parse_op_list_free (op->data.list);
        break;
        }
    case SCRIPT_OP_TYPE_IF:
    case SCRIPT_OP_TYPE_WHILE:
        {
        script_parse_exp_free (op->data.cond_op.cond);
        script_parse_op_free  (op->data.cond_op.op);
        break;
        }
        {
        break;
        }
    case SCRIPT_OP_TYPE_FUNCTION_DEF:
        {
        if (op->data.function_def.function->type == SCRIPT_FUNCTION_TYPE_SCRIPT){
            script_parse_op_free (op->data.function_def.function->data.script);
            }
        ply_list_node_t *node;
        for (node = ply_list_get_first_node (op->data.function_def.function->parameters);
             node;
             node = ply_list_get_next_node (op->data.function_def.function->parameters, node)){
            char* arg = ply_list_node_get_data (node);
            free(arg);
            }
        ply_list_free(op->data.function_def.function->parameters);
        script_parse_exp_free(op->data.function_def.name);
        free(op->data.function_def.function);
        break;
        }
    case SCRIPT_OP_TYPE_RETURN:
        {
        if (op->data.exp) script_parse_exp_free (op->data.exp);
        break;
        }
    case SCRIPT_OP_TYPE_BREAK:
    case SCRIPT_OP_TYPE_CONTINUE:
        {
        break;
        }
    }
 free(op);
}


static void script_parse_op_list_free (ply_list_t* op_list)
{
 ply_list_node_t *node;
 for (node = ply_list_get_first_node (op_list); node; node = ply_list_get_next_node (op_list, node)){
    script_op* op = ply_list_node_get_data (node);
    script_parse_op_free(op);
    }
 ply_list_free(op_list);
 return;
}




script_op* script_parse_file (char* filename)
{
 ply_scan_t* scan = ply_scan_file (filename);
 assert(scan);
 ply_list_t* list = script_parse_op_list (scan);
 ply_scan_free(scan);
 script_op* op = malloc(sizeof(script_op));
 op->type = SCRIPT_OP_TYPE_OP_BLOCK;
 op->data.list = list;
 return op;
}







script_op* script_parse_string (char* string)
{
 ply_scan_t* scan = ply_scan_string (string);
 assert(scan);
 ply_list_t* list = script_parse_op_list (scan);
 ply_scan_free(scan);
 script_op* op = malloc(sizeof(script_op));
 op->type = SCRIPT_OP_TYPE_OP_BLOCK;
 op->data.list = list;
 return op;
}







