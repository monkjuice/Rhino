#include "ForgeSuites.h"
#include "ForgeTestSupport.h"
#include "../ui/ForgeTheme.h"

#include <iostream>

// The theme: its names, its palettes, the file that remembers it, its menu,
// and the panel drawn in Merli. One check holds the area's place in the
// registry until the Merli drawing lands with the suites that judge it.
namespace rhino::forge::tests
{
void themeTests()
{
    std::cout << "Theme\n";
    require(ui::themeFromName(ui::themeName(ui::ThemeId::merli)) == ui::ThemeId::merli,
            "a theme's name reads back as its id");
}
}
