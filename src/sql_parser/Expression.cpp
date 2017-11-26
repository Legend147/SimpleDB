#include <map>
#include <cstring>
#include <cassert>
#include <regex>

#include "Expression.h"

using namespace std;

typedef std::pair<string, ExprVal> table_value_t;
static std::multimap<string, table_value_t> column_cache;

const char *Exception2String[] = {
        "No exception",
        "Different operand type in expression",
        "Illegal operator",
        "Unimplemented yet",
        "Column name not unique",
        "Unknown column"
};

void clean_column_cache() {
    // printf("clean cache all\n");
    column_cache.clear();
}

void clean_column_cache_by_table(const char *table) {
    // printf("clean cache %s\n", table);
    for (auto it = column_cache.begin(); it != column_cache.end();) {
        if (it->second.first == table)
            it = column_cache.erase(it);
        else
            ++it;
    }
}

void update_column_cache(const char *col_name, const char *table, const ExprVal &v) {
    // printf("update cache %s\n", table);
    column_cache.insert(std::make_pair(string(col_name), table_value_t(string(table), v)));
}

void free_expr(expr_node *expr) {
    assert(expr);
    if (expr->op == OPER_NONE) {
        assert(expr->term_type != TERM_NONE);
        if (expr->term_type == TERM_STRING)
            free(expr->literal_s);
        else if (expr->term_type == TERM_COLUMN) {
            if (expr->column->table)
                free(expr->column->table);
            free(expr->column->column);
            free(expr->column);
        }

    } else {
        free_expr(expr->left);
        if (!(expr->op & OPER_UNARY))
            free_expr(expr->right);
    }
    free(expr);
}

bool strlike(const char *a, const char *b) {
    std::string regstr;
    char status = 'A';
    for (int i = 0; i < strlen(b); i++) {
        if (status == 'A') {
            // common status
            if (b[i] == '\\') {
                status = 'B';
            } else if (b[i] == '[') {
                regstr += "[";
                status = 'C';
            } else if (b[i] == '%') {
                regstr += ".*";
            } else if (b[i] == '_') {
                regstr += ".";
            } else {
                regstr += b[i];
            }
        } else if (status == 'B') {
            // after '\'
            if (b[i] == '%' || b[i] == '_' || b[i] == '!') {
                regstr += b[i];
            } else {
                regstr += "\\";
                regstr += b[i];
            }
            status = 'A';
        } else if (status == 'C') {
            // after '[' inside []
            if (b[i] == '!') {
                regstr += "^";
            } else {
                regstr += b[i];
            }
            status = 'A';
        } else
            assert(0);
    }

    std::regex reg(regstr);
    return std::regex_match(std::string(a), reg);
}

ExprVal term2val(expr_node *expr) {
    ExprVal ret;
    ret.type = expr->term_type;
    switch (expr->term_type) {
        case TERM_INT:
            ret.value.value_i = expr->literal_i;
            break;
        case TERM_STRING:
            ret.value.value_s = expr->literal_s;
            break;
        case TERM_DOUBLE:
            ret.value.value_d = expr->literal_d;
            break;
        case TERM_BOOL:
            ret.value.value_b = expr->literal_b;
            break;
        case TERM_COLUMN: {
            int cnt = column_cache.count(string(expr->column->column));
            // printf("count %s = %d\n", expr->column->column, cnt);
            if (!cnt)
                throw (int) EXCEPTION_UNKOWN_COLUMN;
            else if (cnt > 1 && !expr->column->table)
                throw (int) EXCEPTION_COL_NOT_UNIQUE;
            auto it = column_cache.find(string(expr->column->column));
            for (; it != column_cache.end(); ++it)
                if (!expr->column->table || it->second.first == string(expr->column->table)) {
                    ret = it->second.second;
                    goto found_col;
                }
            throw (int) EXCEPTION_UNKOWN_COLUMN;
            found_col:;
        }
            break;
        case TERM_NULL:
            break;
        default:
            assert(0);
    }
    return ret;
}

ExprVal calcExpression(expr_node *expr) {
    assert(expr);
    if (expr->op == OPER_NONE)
        return term2val(expr);
    assert(expr->term_type == TERM_NONE);
    ExprVal result;
    const ExprVal &lv = calcExpression(expr->left);
    const ExprVal &rv = (expr->op & OPER_UNARY) ? ExprVal() : calcExpression(expr->right);
    if (!(expr->op & OPER_UNARY) && rv.type == TERM_NULL) {  // (<anything> <any op> NULL) = NULL
        result.type = TERM_NULL;
        return result;
    }
    if (!(expr->op & OPER_UNARY) && lv.type != rv.type && lv.type != TERM_NULL)
        throw (int) EXCEPTION_DIFF_TYPE;
    if (lv.type == TERM_INT) {
        switch (expr->op) {
            case OPER_ADD:
                result.value.value_i = lv.value.value_i + rv.value.value_i;
                result.type = TERM_INT;
                break;
            case OPER_DEC:
                result.value.value_i = lv.value.value_i - rv.value.value_i;
                result.type = TERM_INT;
                break;
            case OPER_MUL:
                result.value.value_i = lv.value.value_i * rv.value.value_i;
                result.type = TERM_INT;
                break;
            case OPER_DIV:
                result.value.value_i = lv.value.value_i / rv.value.value_i;
                result.type = TERM_INT;
                break;
            case OPER_EQU:
                result.value.value_b = lv.value.value_i == rv.value.value_i;
                result.type = TERM_BOOL;
                break;
            case OPER_GT:
                result.value.value_b = lv.value.value_i > rv.value.value_i;
                result.type = TERM_BOOL;
                break;
            case OPER_GE:
                result.value.value_b = lv.value.value_i >= rv.value.value_i;
                result.type = TERM_BOOL;
                break;
            case OPER_LT:
                result.value.value_b = lv.value.value_i < rv.value.value_i;
                result.type = TERM_BOOL;
                break;
            case OPER_LE:
                result.value.value_b = lv.value.value_i <= rv.value.value_i;
                result.type = TERM_BOOL;
                break;
            case OPER_NEQ:
                result.value.value_b = lv.value.value_i != rv.value.value_i;
                result.type = TERM_BOOL;
                break;
            case OPER_NEG:
                result.value.value_i = -lv.value.value_i;
                result.type = TERM_INT;
                break;
            case OPER_ISNULL:
                result.value.value_b = false;
                result.type = TERM_BOOL;
                break;
            default:
                throw (int) EXCEPTION_ILLEGAL_OP;
        }
    } else if (lv.type == TERM_DOUBLE) {
        switch (expr->op) {
            case OPER_ADD:
                result.value.value_d = lv.value.value_d + rv.value.value_d;
                result.type = TERM_DOUBLE;
                break;
            case OPER_DEC:
                result.value.value_d = lv.value.value_d - rv.value.value_d;
                result.type = TERM_DOUBLE;
                break;
            case OPER_MUL:
                result.value.value_d = lv.value.value_d * rv.value.value_d;
                result.type = TERM_DOUBLE;
                break;
            case OPER_DIV:
                result.value.value_d = lv.value.value_d / rv.value.value_d;
                result.type = TERM_DOUBLE;
                break;
            case OPER_EQU:
                result.value.value_b = lv.value.value_d == rv.value.value_d;
                result.type = TERM_BOOL;
                break;
            case OPER_GT:
                result.value.value_b = lv.value.value_d > rv.value.value_d;
                result.type = TERM_BOOL;
                break;
            case OPER_GE:
                result.value.value_b = lv.value.value_d >= rv.value.value_d;
                result.type = TERM_BOOL;
                break;
            case OPER_LT:
                result.value.value_b = lv.value.value_d < rv.value.value_d;
                result.type = TERM_BOOL;
                break;
            case OPER_LE:
                result.value.value_b = lv.value.value_d <= rv.value.value_d;
                result.type = TERM_BOOL;
                break;
            case OPER_NEQ:
                result.value.value_b = lv.value.value_d != rv.value.value_d;
                result.type = TERM_BOOL;
                break;
            case OPER_NEG:
                result.value.value_d = -lv.value.value_d;
                result.type = TERM_DOUBLE;
                break;
            case OPER_ISNULL:
                result.value.value_b = false;
                result.type = TERM_BOOL;
                break;
            default:
                throw (int) EXCEPTION_ILLEGAL_OP;
        }
    } else if (lv.type == TERM_BOOL) {
        switch (expr->op) {
            case OPER_AND:
                result.value.value_b = lv.value.value_b & rv.value.value_b;
                result.type = TERM_BOOL;
                break;
            case OPER_OR:
                result.value.value_b = lv.value.value_b | rv.value.value_b;
                result.type = TERM_BOOL;
                break;
            case OPER_EQU:
                result.value.value_b = lv.value.value_b == rv.value.value_b;
                result.type = TERM_BOOL;
                break;
            case OPER_NOT:
                result.value.value_b = !lv.value.value_b;
                result.type = TERM_BOOL;
                break;
            case OPER_ISNULL:
                result.value.value_b = false;
                result.type = TERM_BOOL;
                break;
            default:
                throw (int) EXCEPTION_ILLEGAL_OP;
        }
    } else if (lv.type == TERM_STRING) {
        switch (expr->op) {
            case OPER_EQU:
                result.value.value_b = (strcasecmp(lv.value.value_s, rv.value.value_s) == 0);
                result.type = TERM_BOOL;
                break;
            case OPER_NEQ:
                result.value.value_b = (strcasecmp(lv.value.value_s, rv.value.value_s) != 0);
                result.type = TERM_BOOL;
                break;
            case OPER_LIKE:
                result.value.value_b = strlike(lv.value.value_s, rv.value.value_s);
                result.type = TERM_BOOL;
                break;
            case OPER_ISNULL:
                result.value.value_b = false;
                result.type = TERM_BOOL;
                break;
            default:
                throw (int) EXCEPTION_ILLEGAL_OP;
        }
    } else if (lv.type == TERM_NULL) {
        if (expr->op == OPER_ISNULL) {
            result.value.value_b = true;
            result.type = TERM_BOOL;
        } else {
            result.type = TERM_NULL;
        }
    } else {
        assert(0);
    }
    return result;
}

bool ExprVal::operator<(const ExprVal &b) const {
    if (b.type == TERM_NULL || type == TERM_NULL)
        return false;
    if (type != b.type)
        throw (int) EXCEPTION_DIFF_TYPE;
    switch (type) {
        case TERM_INT:
            return value.value_i < b.value.value_i;
            break;
        case TERM_DOUBLE:
            return value.value_d < b.value.value_d;
            break;
        default:
            throw (int) EXCEPTION_ILLEGAL_OP;
    }
}

void ExprVal::operator+=(const ExprVal &b) {
    if (b.type == TERM_NULL || type == TERM_NULL)
        return;
    if (type != b.type)
        throw (int) EXCEPTION_DIFF_TYPE;
    switch (type) {
        case TERM_INT:
            value.value_i += b.value.value_i;
            break;
        case TERM_DOUBLE:
            value.value_d += b.value.value_d;
            break;
        default:
            throw (int) EXCEPTION_ILLEGAL_OP;
    }

}

void ExprVal::operator/=(int div) {
    if (type == TERM_NULL)
        return;
    switch (type) {
        case TERM_INT:
            value.value_d = (double) value.value_i / div; //force convert here!
            type = TERM_DOUBLE;
            break;
        case TERM_DOUBLE:
            value.value_d /= div;
            break;
        default:
            throw (int) EXCEPTION_ILLEGAL_OP;
    }
}