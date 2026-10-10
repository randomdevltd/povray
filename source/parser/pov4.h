// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef POVRAY_PARSER_POV4_H
#define POVRAY_PARSER_POV4_H

#include "parser/configparser.h"

#include "base/stringtypes.h"

namespace pov_parser
{

bool IsPov4File(const pov_base::UCS2String& fileName);

}
// end of namespace pov_parser

#endif // POVRAY_PARSER_POV4_H
