/*
 * Cppcheck - A tool for static C/C++ code analysis
 * Copyright (C) 2007-2021 Cppcheck team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


//---------------------------------------------------------------------------
#include "checkbool.h"

#include "astutils.h"
#include "settings.h"
#include "symboldatabase.h"
#include "token.h"
#include "tokenize.h"

#include <list>
//---------------------------------------------------------------------------

// Register this check class (by creating a static instance of it)
namespace {
    CheckBool instance;
}

static const CWE CWE398(398U);  // Indicator of Poor Code Quality
static const CWE CWE571(571U);  // Expression is Always True
static const CWE CWE587(587U);  // Assignment of a Fixed Address to a Pointer
static const CWE CWE704(704U);  // Incorrect Type Conversion or Cast

static bool isBool(const Variable* var)
{
    return (var && Token::Match(var->typeEndToken(), "bool|_Bool"));
}

//---------------------------------------------------------------------------
void CheckBool::checkIncrementBoolean()
{
    if (!mSettings->severity.isEnabled(Severity::style))
        return;

    const SymbolDatabase *symbolDatabase = mTokenizer->getSymbolDatabase();
    for (const Scope * scope : symbolDatabase->functionScopes) {
        for (const Token* tok = scope->bodyStart->next(); tok != scope->bodyEnd; tok = tok->next()) {
            if (astIsBool(tok) && tok->astParent() && tok->astParent()->str() == "++") {
                incrementBooleanError(tok);
            }
        }
    }
}

void CheckBool::incrementBooleanError(const Token *tok)
{
    reportError(
        tok,
        Severity::style,
        "incrementboolean",
        "Incrementing a variable of type 'bool' with postfix operator++ is deprecated by the C++ Standard. You should assign it the value 'true' instead.\n"
        "The operand of a postfix increment operator may be of type bool but it is deprecated by C++ Standard (Annex D-1) and the operand is always set to true. You should assign it the value 'true' instead.",
        CWE398, Certainty::normal
        );
}

//---------------------------------------------------------------------------
// if (bool & bool) -> if (bool && bool)
// if (bool | bool) -> if (bool || bool)
//---------------------------------------------------------------------------
void CheckBool::checkBitwiseOnBoolean()
{
    if (!mSettings->severity.isEnabled(Severity::style))
        return;

    // danmar: this is inconclusive because I don't like that there are
    //         warnings for calculations. Example: set_flag(a & b);
    if (!mSettings->certainty.isEnabled(Certainty::inconclusive))
        return;

    const SymbolDatabase *symbolDatabase = mTokenizer->getSymbolDatabase();
    for (const Scope * scope : symbolDatabase->functionScopes) {
        for (const Token* tok = scope->bodyStart->next(); tok != scope->bodyEnd; tok = tok->next()) {
            if (tok->isBinaryOp() && (tok->str() == "&" || tok->str() == "|")) {
                if (astIsBool(tok->astOperand1()) || astIsBool(tok->astOperand2())) {
                    if (tok->astOperand2()->variable() && tok->astOperand2()->variable()->nameToken() == tok->astOperand2())
                        continue;
                    const std::string expression = astIsBool(tok->astOperand1()) ? tok->astOperand1()->expressionString() : tok->astOperand2()->expressionString();
                    bitwiseOnBooleanError(tok, expression, tok->str() == "&" ? "&&" : "||");
                }
            }
        }
    }
}

void CheckBool::bitwiseOnBooleanError(const Token *tok, const std::string &expression, const std::string &op)
{
    reportError(tok, Severity::style, "bitwiseOnBoolean",
                "Boolean expression '" + expression + "' is used in bitwise operation. Did you mean '" + op + "'?",
                CWE398,
                Certainty::inconclusive);
}

//---------------------------------------------------------------------------
//    if (!x==3) <- Probably meant to be "x!=3"
//---------------------------------------------------------------------------

void CheckBool::checkComparisonOfBoolWithInt()
{
    if (!mSettings->severity.isEnabled(Severity::warning) || !mTokenizer->isCPP())
        return;

    const SymbolDatabase* const symbolDatabase = mTokenizer->getSymbolDatabase();
    for (const Scope * scope : symbolDatabase->functionScopes) {
        for (const Token* tok = scope->bodyStart->next(); tok != scope->bodyEnd; tok = tok->next()) {
            if (!tok->isComparisonOp() || !tok->isBinaryOp())
                continue;
            const Token* const left = tok->astOperand1();
            const Token* const right = tok->astOperand2();
            if (left->isBoolean() && right->varId()) { // Comparing boolean constant with variable
                if (tok->str() != "==" && tok->str() != "!=") {
                    comparisonOfBoolWithInvalidComparator(right, left->str());
                }
            } else if (left->varId() && right->isBoolean()) { // Comparing variable with boolean constant
                if (tok->str() != "==" && tok->str() != "!=") {
                    comparisonOfBoolWithInvalidComparator(right, left->str());
                }
            }
        }
    }
}

void CheckBool::comparisonOfBoolWithInvalidComparator(const Token *tok, const std::string &expression)
{
    reportError(tok, Severity::warning, "comparisonOfBoolWithInvalidComparator",
                "Comparison of a boolean value using relational operator (<, >, <= or >=).\n"
                "The result of the expression '" + expression + "' is of type 'bool'. "
                "Comparing 'bool' value using relational (<, >, <= or >=)"
                " operator could cause unexpected results.");
}

//-------------------------------------------------------------------------------
// Comparing functions which are returning value of type bool
//-------------------------------------------------------------------------------

static bool tokenIsFunctionReturningBool(const Token* tok)
{
    const Function* func = tok->function();
    if (func && Token::Match(tok, "%name% (")) {
        if (func->tokenDef && Token::Match(func->tokenDef->previous(), "bool|_Bool")) {
            return true;
        }
    }
    return false;
}

void CheckBool::checkComparisonOfFuncReturningBool()
{
    if (!mSettings->severity.isEnabled(Severity::style))
        return;

    if (!mTokenizer->isCPP())
        return;

    const SymbolDatabase * const symbolDatabase = mTokenizer->getSymbolDatabase();

    for (const Scope * scope : symbolDatabase->functionScopes) {
        for (const Token* tok = scope->bodyStart->next(); tok != scope->bodyEnd; tok = tok->next()) {
            if (!tok->isComparisonOp() || tok->str() == "==" || tok->str() == "!=")
                continue;
            const Token *firstToken = tok->previous();
            if (tok->strAt(-1) == ")") {
                firstToken = firstToken->link()->previous();
            }
            const Token *secondToken = tok->next();
            while (secondToken->str() == "!") {
                secondToken = secondToken->next();
            }
            const bool firstIsFunctionReturningBool = tokenIsFunctionReturningBool(firstToken);
            const bool secondIsFunctionReturningBool = tokenIsFunctionReturningBool(secondToken);
            if (firstIsFunctionReturningBool && secondIsFunctionReturningBool) {
                comparisonOfTwoFuncsReturningBoolError(firstToken->next(), firstToken->str(), secondToken->str());
            } else if (firstIsFunctionReturningBool) {
                comparisonOfFuncReturningBoolError(firstToken->next(), firstToken->str());
            } else if (secondIsFunctionReturningBool) {
                comparisonOfFuncReturningBoolError(secondToken->previous(), secondToken->str());
            }
        }
    }
}

void CheckBool::comparisonOfFuncReturningBoolError(const Token *tok, const std::string &expression)
{
    reportError(tok, Severity::style, "comparisonOfFuncReturningBoolError",
                "Comparison of a function returning boolean value using relational (<, >, <= or >=) operator.\n"
                "The return type of function '" + expression + "' is 'bool' "
                "and result is of type 'bool'. Comparing 'bool' value using relational (<, >, <= or >=)"
                " operator could cause unexpected results.", CWE398, Certainty::normal);
}

void CheckBool::comparisonOfTwoFuncsReturningBoolError(const Token *tok, const std::string &expression1, const std::string &expression2)
{
    reportError(tok, Severity::style, "comparisonOfTwoFuncsReturningBoolError",
                "Comparison of two functions returning boolean value using relational (<, >, <= or >=) operator.\n"
                "The return type of function '" + expression1 + "' and function '" + expression2 + "' is 'bool' "
                "and result is of type 'bool'. Comparing 'bool' value using relational (<, >, <= or >=)"
                " operator could cause unexpected results.", CWE398, Certainty::normal);
}

//-------------------------------------------------------------------------------
// Comparison of bool with bool
//-------------------------------------------------------------------------------

void CheckBool::checkComparisonOfBoolWithBool()
{
    // FIXME: This checking is "experimental" because of the false positives
    //        when self checking lib/tokenize.cpp (#2617)
    if (!mSettings->certainty.isEnabled(Certainty::experimental))
        return;

    if (!mSettings->severity.isEnabled(Severity::style))
        return;

    if (!mTokenizer->isCPP())
        return;

    const SymbolDatabase* const symbolDatabase = mTokenizer->getSymbolDatabase();

    for (const Scope * scope : symbolDatabase->functionScopes) {
        for (const Token* tok = scope->bodyStart->next(); tok != scope->bodyEnd; tok = tok->next()) {
            if (!tok->isComparisonOp() || tok->str() == "==" || tok->str() == "!=")
                continue;
            bool firstTokenBool = false;

            const Token *firstToken = tok->previous();
            if (firstToken->varId()) {
                if (isBool(firstToken->variable())) {
                    firstTokenBool = true;
                }
            }
            if (!firstTokenBool)
                continue;

            bool secondTokenBool = false;
            const Token *secondToken = tok->next();
            if (secondToken->varId()) {
                if (isBool(secondToken->variable())) {
                    secondTokenBool = true;
                }
            }
            if (secondTokenBool) {
                comparisonOfBoolWithBoolError(firstToken->next(), secondToken->str());
            }
        }
    }
}

void CheckBool::comparisonOfBoolWithBoolError(const Token *tok, const std::string &expression)
{
    reportError(tok, Severity::style, "comparisonOfBoolWithBoolError",
                "Comparison of a variable having boolean value using relational (<, >, <= or >=) operator.\n"
                "The variable '" + expression + "' is of type 'bool' "
                "and comparing 'bool' value using relational (<, >, <= or >=)"
                " operator could cause unexpected results.", CWE398, Certainty::normal);
}

//-----------------------------------------------------------------------------
void CheckBool::checkAssignBoolToPointer()
{
    const SymbolDatabase *symbolDatabase = mTokenizer->getSymbolDatabase();
    for (const Scope * scope : symbolDatabase->functionScopes) {
        for (const Token* tok = scope->bodyStart; tok != scope->bodyEnd; tok = tok->next()) {
            if (tok->str() == "=" && astIsPointer(tok->astOperand1()) && astIsBool(tok->astOperand2())) {
                assignBoolToPointerError(tok);
            }
        }
    }
}

void CheckBool::assignBoolToPointerError(const Token *tok)
{
    reportError(tok, Severity::error, "assignBoolToPointer",
                "Boolean value assigned to pointer.", CWE587, Certainty::normal);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CheckBool::checkComparisonOfBoolExpressionWithInt()
{
    if (!mSettings->severity.isEnabled(Severity::warning))
        return;

    const SymbolDatabase* symbolDatabase = mTokenizer->getSymbolDatabase();

    for (const Scope * scope : symbolDatabase->functionScopes) {
        for (const Token* tok = scope->bodyStart->next(); tok != scope->bodyEnd; tok = tok->next()) {
            if (!tok->isComparisonOp())
                continue;

            const Token* numTok = nullptr;
            const Token* boolExpr = nullptr;
            bool numInRhs;
            if (astIsBool(tok->astOperand1())) {
                boolExpr = tok->astOperand1();
                numTok = tok->astOperand2();
                numInRhs = true;
            } else if (astIsBool(tok->astOperand2())) {
                boolExpr = tok->astOperand2();
                numTok = tok->astOperand1();
                numInRhs = false;
            } else {
                continue;
            }

            if (!numTok || !boolExpr)
                continue;

            if (boolExpr->isOp() && numTok->isName() && Token::Match(tok, "==|!="))
                // there is weird code such as:  ((a<b)==c)
                // but it is probably written this way by design.
                continue;

            if (astIsBool(numTok))
                continue;

            const ValueFlow::Value *minval = numTok->getValueLE(0, mSettings);
            if (minval && minval->intvalue == 0 &&
                (numInRhs ? Token::Match(tok, ">|==|!=")
                 : Token::Match(tok, "<|==|!=")))
                minval = nullptr;

            const ValueFlow::Value *maxval = numTok->getValueGE(1, mSettings);
            if (maxval && maxval->intvalue == 1 &&
                (numInRhs ? Token::Match(tok, "<|==|!=")
                 : Token::Match(tok, ">|==|!=")))
                maxval = nullptr;

            if (minval || maxval) {
                bool not0or1 = (minval && minval->intvalue < 0) || (maxval && maxval->intvalue > 1);
                comparisonOfBoolExpressionWithIntError(tok, not0or1);
            }
        }
    }
}

void CheckBool::comparisonOfBoolExpressionWithIntError(const Token *tok, bool not0or1)
{
    if (not0or1)
        reportError(tok, Severity::warning, "compareBoolExpressionWithInt",
                    "Comparison of a boolean expression with an integer other than 0 or 1.", CWE398, Certainty::normal);
    else
        reportError(tok, Severity::warning, "compareBoolExpressionWithInt",
                    "Comparison of a boolean expression with an integer.", CWE398, Certainty::normal);
}


void CheckBool::pointerArithBool()
{
    const SymbolDatabase* symbolDatabase = mTokenizer->getSymbolDatabase();

    for (const Scope &scope : symbolDatabase->scopeList) {
        if (scope.type != Scope::eIf && !scope.isLoopScope())
            continue;
        const Token* tok = scope.classDef->next()->astOperand2();
        if (scope.type == Scope::eFor) {
            tok = Token::findsimplematch(scope.classDef->tokAt(2), ";");
            if (tok)
                tok = tok->astOperand2();
            if (tok)
                tok = tok->astOperand1();
        } else if (scope.type == Scope::eDo)
            tok = (scope.bodyEnd->tokAt(2)) ? scope.bodyEnd->tokAt(2)->astOperand2() : nullptr;

        pointerArithBoolCond(tok);
    }
}

void CheckBool::pointerArithBoolCond(const Token *tok)
{
    if (!tok)
        return;
    if (Token::Match(tok, "&&|%oror%")) {
        pointerArithBoolCond(tok->astOperand1());
        pointerArithBoolCond(tok->astOperand2());
        return;
    }
    if (tok->str() != "+" && tok->str() != "-")
        return;

    if (tok->isBinaryOp() &&
        tok->astOperand1()->isName() &&
        tok->astOperand1()->variable() &&
        tok->astOperand1()->variable()->isPointer() &&
        tok->astOperand2()->isNumber())
        pointerArithBoolError(tok);
}

void CheckBool::pointerArithBoolError(const Token *tok)
{
    reportError(tok,
                Severity::error,
                "pointerArithBool",
                "Converting pointer arithmetic result to bool. The bool is always true unless there is undefined behaviour.\n"
                "Converting pointer arithmetic result to bool. The boolean result is always true unless there is pointer arithmetic overflow, and overflow is undefined behaviour. Probably a dereference is forgotten.", CWE571, Certainty::normal);
}

void CheckBool::checkAssignBoolToFloat()
{
    if (!mTokenizer->isCPP())
        return;
    if (!mSettings->severity.isEnabled(Severity::style))
        return;
    const SymbolDatabase *symbolDatabase = mTokenizer->getSymbolDatabase();
    for (const Scope * scope : symbolDatabase->functionScopes) {
        for (const Token* tok = scope->bodyStart; tok != scope->bodyEnd; tok = tok->next()) {
            if (tok->str() == "=" && astIsFloat(tok->astOperand1(), false) && astIsBool(tok->astOperand2())) {
                assignBoolToFloatError(tok);
            }
        }
    }
}

void CheckBool::assignBoolToFloatError(const Token *tok)
{
    reportError(tok, Severity::style, "assignBoolToFloat",
                "Boolean value assigned to floating point variable.", CWE704, Certainty::normal);
}

void CheckBool::returnValueOfFunctionReturningBool()
{
    if (!mSettings->severity.isEnabled(Severity::style))
        return;

    const SymbolDatabase * const symbolDatabase = mTokenizer->getSymbolDatabase();

    for (const Scope * scope : symbolDatabase->functionScopes) {
        if (!(scope->function && Token::Match(scope->function->retDef, "bool|_Bool")))
            continue;

        for (const Token* tok = scope->bodyStart->next(); tok && (tok != scope->bodyEnd); tok = tok->next()) {
            // Skip lambdas
            const Token* tok2 = findLambdaEndToken(tok);
            if (tok2)
                tok = tok2;
            else if (tok->scope() && tok->scope()->isClassOrStruct())
                tok = tok->scope()->bodyEnd;
            else if (Token::simpleMatch(tok, "return") && tok->astOperand1() &&
                     (tok->astOperand1()->getValueGE(2, mSettings) || tok->astOperand1()->getValueLE(-1, mSettings)) &&
                     !(tok->astOperand1()->astOperand1() && Token::Match(tok->astOperand1(), "&|%or%")))
                returnValueBoolError(tok);
        }
    }
}

void CheckBool::returnValueBoolError(const Token *tok)
{
    reportError(tok, Severity::style, "returnNonBoolInBooleanFunction", "Non-boolean value returned from function returning bool");
}
