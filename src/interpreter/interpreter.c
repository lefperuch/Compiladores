#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interpreter.h"

extern LiteralsTable literals;
extern VariablesTable variables;
extern FunctionsTable functions;

static void _recursively_run_node (AST *);

#ifndef _CC_MAX_FRAME_STACK_SIZE
    #define _CC_MAX_FRAME_STACK_SIZE_ 128
#endif

//preludio: alocar frame e copiar parametros para o novo frame, depois executar o func_decl (prelúdio executa no fcall)

struct frame_t {
    // Índices da store que pertencem ao frame em questão.
    // Qualquer índice que esteja setado com `false` é proveniente
    // de uma passagem de parâmetro por referência e não deve
    // ser contabilizado na hora de liberar a memória alocada
    // durante a inicialização do frame.
    bool *owned_indexes;

    // Região que armazena todas as variáveis locais da função.
    // Ela é composta de ponteiros para inteiros. Caso a variável seja 
    // dos tipos `VT_INT` ou `VT_ARRAY`, o espaço é alocado in-place,
    // onde cada posição da store corresponde a uma variável.
    // Nos casos de variáveis do tipo `VT_ARRAY_POINTER`, a store armazena um
    // ponteiro para o início do vetor no frame original.
    int **store;

    // Pilha que armazena os resultados das expressões da função.
    int stack[_CC_MAX_FRAME_STACK_SIZE_];
    // Stack pointer do frame.
    int frame_sp;

    // Tamanho da store.
    int store_size;
    
    struct frame_t *next;
};

struct frame_stack_t {
    struct frame_t *top;
    size_t size;
};

static struct frame_t *current_frame;
static struct frame_stack_t *frame_stack;

static void _frame_stack_push (struct frame_t *frame, int data) {
    if(frame->frame_sp == _CC_MAX_FRAME_STACK_SIZE_ - 1) {
        printf("PILHA CHEIA %s:%d\n", __FILE__, __LINE__);
        exit(-1);
    }

    ++frame->frame_sp;
    frame->stack[frame->frame_sp] = data;
}

static int _frame_stack_pop (struct frame_t *frame) {
    if(frame->frame_sp < 0) {
        printf("PILHA VAZIA %s:%d\n", __FILE__, __LINE__);
        exit(-1);
    }

    int data = frame->stack[frame->frame_sp];
    --frame->frame_sp;

    return data;
}

static AST *map_ast_nodes_to_functions_table (AST *root) {
    AST *main_fn = NULL;

    // Percorre filhos do nó func-list
    for(AST *child = root->getChildren(root); child != NULL; child = child->next_sibiling) {
        AST *fn_header = child->getChildren(child);
        
        ft_node_t *fn = AST_GET_NODE_DATA(
            fn_header->getChildren(fn_header)
        );

        function_set_ast_node(fn, child);

        if(strcmp(fn->identifier, "main") == 0) {
            main_fn = child;
        }
    }

    return main_fn;
}

struct frame_t *_pop_frame (struct frame_stack_t *fstack) {
    if(fstack->size == 0) {
        return NULL;
    }

    struct frame_t *frame = fstack->top;

    fstack->top = frame->next;
    --fstack->size;

    return frame;
}

void _push_frame (struct frame_stack_t *fstack, struct frame_t *frame) {
    frame->next = fstack->top;
    fstack->top = frame;
    ++fstack->size;
}

static void _frame_add_var (struct frame_t *frame, vt_node_t *var) {
    switch(var->type) {
        case VT_INT:
            // Caso seja um int, é uma variável comum e um espaço de memória deve ser criado.
            frame->owned_indexes[var->frame_offset] = true;
            frame->store[var->frame_offset] = calloc(1, sizeof **frame->store);    
            break;
        case VT_ARRAY:
            // Caso seja um array, é uma variável comum e um espaço do tamanho do array deve ser criado.
            frame->owned_indexes[var->frame_offset] = true;
            frame->store[var->frame_offset] = calloc(var->size, sizeof **frame->store);
            break;
        case VT_ARRAY_POINTER:
            // Caso seja uma referência, não é inicializada uma posição de memória para esta
            // variável e uma flag é setada para indicar que a variável pertence a outro frame.
            frame->owned_indexes[var->frame_offset] = false;
            frame->store[var->frame_offset] = NULL;
            break;
    }
}

static void _run_plus (AST *plus) {
    #ifdef DEBUG
        printf("_run_plus\n");
    #endif

    int r = _frame_stack_pop(current_frame);
    int l = _frame_stack_pop(current_frame);

    _frame_stack_push(current_frame, l + r);
}

static void _run_over (AST *over) {
    #ifdef DEBUG
        printf("_run_over\n");
    #endif

    int r = _frame_stack_pop(current_frame);
    int l = _frame_stack_pop(current_frame);

    _frame_stack_push(current_frame, l / r);
}

static void _run_times (AST *over) {
    #ifdef DEBUG
        printf("_run_times\n");
    #endif

    int r = _frame_stack_pop(current_frame);
    int l = _frame_stack_pop(current_frame);

    _frame_stack_push(current_frame, l * r);
}

static void _run_minus (AST *sub) {
    #ifdef DEBUG
        printf("_run_minus\n");
    #endif

    int r = _frame_stack_pop(current_frame);
    int l = _frame_stack_pop(current_frame);

    _frame_stack_push(current_frame, l - r);
}

static void _run_gt (AST *gt) {
    #ifdef DEBUG
        printf("_run_gt\n");
    #endif

    int r = _frame_stack_pop(current_frame);
    int l = _frame_stack_pop(current_frame);

    _frame_stack_push(current_frame, l > r);
}

static void _run_ge (AST *ge) {
    #ifdef DEBUG
        printf("_run_ge\n");
    #endif

    int r = _frame_stack_pop(current_frame);
    int l = _frame_stack_pop(current_frame);

    _frame_stack_push(current_frame, l >= r);
}

static void _run_lt (AST *lt) {
    #ifdef DEBUG
        printf("_run_lt\n");
    #endif

    int r = _frame_stack_pop(current_frame);
    int l = _frame_stack_pop(current_frame);

    _frame_stack_push(current_frame, l < r);
}

static void _run_le (AST *le) {
    #ifdef DEBUG
        printf("_run_le\n");
    #endif

    int r = _frame_stack_pop(current_frame);
    int l = _frame_stack_pop(current_frame);

    _frame_stack_push(current_frame, l <= r);
}

static void _run_eq (AST *eq) {
    #ifdef DEBUG
        printf("_run_eq\n");
    #endif

    int r = _frame_stack_pop(current_frame);
    int l = _frame_stack_pop(current_frame);

    _frame_stack_push(current_frame, l == r);
}

static void _run_neq (AST *neq) {
    #ifdef DEBUG
        printf("_run_neq\n");
    #endif

    int r = _frame_stack_pop(current_frame);
    int l = _frame_stack_pop(current_frame);

    _frame_stack_push(current_frame, l != r);
}

static void _run_num (AST *num) {
    #ifdef DEBUG
        printf("_run_num ");
    #endif

    int *data = AST_GET_NODE_DATA(num);

    #ifdef DEBUG
        printf("%d\n", *data);
    #endif

    _frame_stack_push(current_frame, *data);
}

static void _run_output (AST *output) {
    #ifdef DEBUG
        printf("_run_output\n");
    #endif

    int data = _frame_stack_pop(current_frame);

    printf("%d", data);
}

static void _run_if (AST *_if) {
    #ifdef DEBUG
        printf("_run_if: ");
    #endif
    
    AST *expr = _if->getChildren(_if);
    int r = _frame_stack_pop(current_frame);

    if(r == 0) {
        #ifdef DEBUG
            printf("false\n");
        #endif
        
        AST *_else = expr->next_sibiling->next_sibiling;
        _recursively_run_node(_else);
    } else {
        #ifdef DEBUG
            printf("true\n");
        #endif
        AST *_true = expr->next_sibiling;
        _recursively_run_node(_true);
    }
}

static void _run_while (AST *_while) {
    #ifdef DEBUG
        printf("_run_while: ");
    #endif
    
    AST *expr = _while->getChildren(_while);
    int r = _frame_stack_pop(current_frame);

    // Enquanto a expressão for verdadeira.
    while(r != 0) {
        // Executa o bloco recursivamente.
        _recursively_run_node(expr->next_sibiling);
        // Calcula o valor da condição.
        _recursively_run_node(expr);
        r = _frame_stack_pop(current_frame);
    }
}

static void _run_assign (AST *assign) {
    #ifdef DEBUG
        printf("_run_assign\n");
    #endif

    AST *lval = assign->getChildren(assign);
    AST *rval = lval->next_sibiling;

    #ifdef DEBUG
        printf("_run_assign_rval\n");
    #endif
    // Recursivamente calcula o rval
    _recursively_run_node(rval);

    #ifdef DEBUG
        printf("_run_assign_lval_idx\n");
    #endif
    // Recursivamente executa o cálculo do índice, caso seja um array
    _recursively_run_node(lval->getChildren(lval));

    vt_node_t *var = AST_GET_NODE_DATA(lval);

    switch(var->type) {
        case VT_INT:
            *(current_frame->store[var->frame_offset]) = _frame_stack_pop(current_frame);
            break;
        case VT_ARRAY:
        case VT_ARRAY_POINTER: ;
            int idx = _frame_stack_pop(current_frame);
            int val = _frame_stack_pop(current_frame);
            
            current_frame->store[var->frame_offset][idx] = val;
            break;
    }
}

static char const *_print_special_char (char const *ch) {
    char p = *ch;

    switch(p) {
        case 'n': printf("\n");
            break;
        case 't': printf("\t");
            break;
    }

    return ch;
}

static void _run_write (AST *write) {
    #ifdef DEBUG
        printf("_run_write\n");
    #endif

    lt_node_t *string = AST_GET_NODE_DATA(write->getChildren(write));

    for(char const *p = string->value; *p; ++p) {
        if(*p == '"') {
            continue;
        }

        if(*p == '\\') {
            p = _print_special_char(p + 1);
        } else {
            printf("%c", *p);
        }
    }
}

static void _run_input (AST *input) {
    #ifdef DEBUG
        printf("_run_input\n");
    #endif

    int val;

    scanf("%d", &val);

    _frame_stack_push(current_frame, val);
}

static void _run_var_use (AST *var_node) {
    #ifdef DEBUG
        printf("_run_var_use\n");
    #endif
    vt_node_t *var = AST_GET_NODE_DATA(var_node);
    
    // Executa os filhos, caso hajam
    // Isso calcula, por exemplo, índices de array.
    _recursively_run_node(var_node->getChildren(var_node));

    switch(var->type) {
        case VT_INT:
            _frame_stack_push(current_frame, *(current_frame->store[var->frame_offset]));
            break;
        case VT_ARRAY:
        case VT_ARRAY_POINTER: ;
            int idx = _frame_stack_pop(current_frame);
            _frame_stack_push(current_frame, current_frame->store[var->frame_offset][idx]);
            break;
    }
}

static _operation_fn_t _get_node_operation_fn (AST *node) {
    switch(AST_GET_NODE_TYPE(node)) {
        case AST_NODE_WRITE: return _run_write;
        case AST_NODE_ASSIGN: return _run_assign;
        case AST_NODE_OUTPUT: return _run_output;
        case AST_NODE_PLUS: return _run_plus;
        case AST_NODE_OVER: return _run_over;
        case AST_NODE_TIMES: return _run_times;
        case AST_NODE_MINUS: return _run_minus;
        case AST_NODE_NUM: return _run_num;
        case AST_NODE_VAR_USE: return _run_var_use;
        case AST_NODE_GT: return _run_gt;
        case AST_NODE_GE: return _run_ge;
        case AST_NODE_LT: return _run_lt;
        case AST_NODE_LE: return _run_le;
        case AST_NODE_EQ: return _run_eq;
        case AST_NODE_NEQ: return _run_neq;
        case AST_NODE_IF: return _run_if;
        case AST_NODE_WHILE: return _run_while;
        case AST_NODE_INPUT: return _run_input;
        case AST_NODE_FUNC_CALL:
        default:
            // AST_NODE_STRING
            return NULL;
    }
}

static void print_stack (struct frame_t *frame) {
    printf("frame stack state: ");
    for(int i = 0; i < frame->frame_sp; ++i) {
        printf("%d ", frame->stack[i]);
    }
    printf("\n");
}

static void _recursively_run_node (AST *node) {
    if(node == NULL) return;
    
    #ifdef DEBUG
        //print_stack(current_frame);
    #endif

    AST *child = NULL;
    ASTNodeType type = AST_GET_NODE_TYPE(node);

    if(type == AST_NODE_VAR_USE || type == AST_NODE_ASSIGN || type == AST_NODE_FUNC_CALL) {
        // Operações especiais, não executa os filhos desses nós.
    } else if(type == AST_NODE_IF || type == AST_NODE_WHILE) {
        //recursivamente executa o nó da expressão
        child = node->getChildren(node);
        _recursively_run_node(child);
    } else {
        //Caminhamento pós ordem pelos filhos do bloco.
        for(child = node->getChildren(node); child != NULL; child = child->next_sibiling) {
            _recursively_run_node(child);
        }
    }

    // Obtém a função que deve executar o nó atual.
    _operation_fn_t op_fn = _get_node_operation_fn(node);

    // Executa o nó atual.
    if(op_fn != NULL) {
        op_fn(node);
    }

    #ifdef DEBUG
        printf("end run\n");
    #endif
}

static void _run_func_body (AST *fn_body) {
    AST *block = (fn_body->getChildren(fn_body))->next_sibiling;

    _recursively_run_node(block);
    _operation_fn_t op_fn = _get_node_operation_fn(block);

    if(op_fn != NULL) {
        op_fn(block);
    }
}

static struct frame_t *_initialize_frame (AST *fn_node) {
    AST *header = fn_node->getChildren(fn_node);
    ft_node_t *fn = AST_GET_NODE_DATA(header->getChildren(header));
    struct frame_t *frame = calloc(1, sizeof *frame);

    frame->frame_sp = -1;
    frame->store_size = fn->frame_store_size;
    frame->owned_indexes = calloc(frame->store_size, sizeof *frame->owned_indexes);
    frame->store = calloc(frame->store_size, sizeof *frame->store);

    short i;
    AST *tmp;
    AST *param_list = header->getChildren(header)->next_sibiling;
    
    // Percorrer param_list.
    for(i = 0, tmp = param_list->getChildren(param_list); tmp != NULL; ++i, tmp = tmp->next_sibiling) {
        vt_node_t *var = AST_GET_NODE_DATA(tmp);
        
        _frame_add_var(frame, var);
    }

    AST *body = header->next_sibiling;
    AST *var_list = body->getChildren(body); 

    for(tmp = var_list->getChildren(var_list); tmp != NULL; ++i, tmp = tmp->next_sibiling) {
        vt_node_t *var = AST_GET_NODE_DATA(tmp);
        
        _frame_add_var(frame, var);
    }

    return frame;
}

static void _delete_frame (struct frame_t *frame) {
    int i;

    for(i = 0; i < frame->store_size; ++ i) {
        if(frame->owned_indexes[i] == true) {
            free(frame->store[i]);
        }
    }

    free(frame->store);
    free(frame->owned_indexes);
    free(frame);
}

static void _run_ast_sub (AST *main) {
    stdin = fopen(ctermid(NULL), "r");
    frame_stack = calloc(1, sizeof *frame_stack);
    
    AST *fn_header = main->getChildren(main);
    struct frame_t *frame = _initialize_frame(main);

    current_frame = frame;

    _push_frame(frame_stack, frame);
    _run_func_body(fn_header->next_sibiling);
    frame = _pop_frame(frame_stack);

    _delete_frame(frame);
}

void run_ast (AST *root) {
    // Faz o mapeamento dos nós da AST para a tabela de funções e retorna a referência para a função main.
    AST *main_fn = map_ast_nodes_to_functions_table(root);

    if(main_fn == NULL) {
        printf("ERROR: undefined reference to function `main`.\n");
        exit(-1);
    }

    _run_ast_sub(main_fn);
}
