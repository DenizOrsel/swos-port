#include "CareerTest.h"
#include "unitTest.h"
#include "menus.h"
#include "swossym.h"

static CareerTest t;

const char *CareerTest::name() const
{
    return "career";
}

const char *CareerTest::displayName() const
{
    return "career mode test";
}

void CareerTest::init()
{
    takeOverInput();
}

void CareerTest::finish()
{
    SWOS_UnitTest::setMenuCallback();
}

auto CareerTest::getCases() -> CaseList
{
    return {
        { "test career Germany Bayern", "career-germany-bayern", {}, bind(&CareerTest::testCareerGermany), 1, false },
    };
}

void CareerTest::testCareerGermany()
{
    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });

    swos.coachOrPlayer = 0;
    D0 = 0x030E;
    InitCareer();
    SWOS_UnitTest::setMenuCallback();
}
