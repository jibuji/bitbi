// Copyright (c) 2023 Bitbi Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "logprintf.h"

#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

class BitbiModule final : public clang::tidy::ClangTidyModule
{
public:
    void addCheckFactories(clang::tidy::ClangTidyCheckFactories& CheckFactories) override
    {
        CheckFactories.registerCheck<bitbi::LogPrintfCheck>("bitbi-unterminated-logprintf");
    }
};

static clang::tidy::ClangTidyModuleRegistry::Add<BitbiModule>
    X("bitbi-module", "Adds bitbi checks.");

volatile int BitbiModuleAnchorSource = 0;
