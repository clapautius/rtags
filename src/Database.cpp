#include "Database.h"

const char * Database::Cursor::kindToString(Kind kind)
{
    const char *names[] = {
        "Invalid",
        "MemberFunctionDefinition",
        "MemberFunctionDeclaration",
        "MethodDefinition",
        "MethodDeclaration",
        "Class",
        "Namespace",
        "Struct",
        "Variable",
        "Parameter",
        "Field",
        "Enum",
        "EnumValue",
        "Macro",
        "Reference",
        0
    };
    return names[kind];
}

String Database::Cursor::toString(unsigned flags) const
{
    String ret = String::format<1024>("SymbolName: %s\n"
                                      "Kind: %s\n"
                                      "%s" // range
                                      "%s", // definition
                                      symbolName.constData(),
                                      kindToString(kind),
                                      (start != -1 && end != -1 ? String::format<32>("Range: %d-%d\n", start, end).constData() : ""),
                                      isDefinition() ? "Definition\n" : "");

    if (!target.isEmpty() && flags & IncludeTarget) {
        ret.append("Target: ");
        ret.append(target.key(flags));
    }
    if (!references.isEmpty() && !(flags & IncludeReferences)) {
        ret.append("References:");
        for (Set<Location>::const_iterator rit = references.begin(); rit != references.end(); ++rit) {
            const Location &l = *rit;
            ret.append("\n    ");
            ret.append(l.key(flags));
        }
        ret.append('\n');
    }
    return ret;
}

bool Database::Cursor::isDefinition() const
{
    switch (kind) {
    case Invalid:
    case MemberFunctionDeclaration:
    case MethodDeclaration:
    case Reference:
        return false;
    case Macro:
    case MemberFunctionDefinition:
    case MethodDefinition:
    case Class:
    case Namespace:
    case Struct:
    case Variable:
    case Parameter:
    case Field:
    case Enum:
    case EnumValue:
        return true;
    }
    return false;
}