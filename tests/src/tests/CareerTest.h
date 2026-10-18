#pragma once

#include "BaseTest.h"

class CareerTest : public BaseTest
{
public:
    void init() override;
    void finish() override;
    void defaultCaseInit() override {}
    const char *name() const override;
    const char *displayName() const override;
    CaseList getCases() override;

private:
    void testCareerGermany();
    void testCareerLiverpool();
};
