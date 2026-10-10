[(line_comment) (block_comment)] @comment

(string) @string
(number) @number
(constant) @constant.builtin
(builtin_function) @function.builtin

(macro_directive name: (identifier) @function)
(call_expression function: (identifier) @function.call)
(parameter name: (identifier) @variable.parameter)
(declare_directive target: (identifier) @variable)
(member_expression member: (identifier) @property)
(dictionary_entry key: (identifier) @property)
(block name: (identifier) @type)

(keyword) @keyword
(colour_component channel: _ @keyword)
["color" "colour" "function" "array" "dictionary" "mixed" "optional" "deprecated" "once"
 "read" "write" "append"] @keyword

["#declare" "#local" "#macro" "#end" "#if" "#ifdef" "#ifndef" "#elseif" "#else" "#while" "#for" "#switch" "#case"
 "#range" "#break" "#include" "#version" "#default" "#undef" "#debug" "#warning" "#error" "#fopen" "#fclose"
 "#read" "#write"] @keyword.directive

(vector ["<" ">"] @punctuation.bracket)
["+" "-" "*" "/" "<" "<=" "=" "!=" ">=" ">" "&" "|" "!" "?" ":"] @operator
["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["," ";" "."] @punctuation.delimiter
