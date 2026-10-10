(macro_directive name: (identifier) @name) @definition.function
(declare_directive target: (identifier) @name value: (function_block)) @definition.function
(declare_directive target: (identifier) @name value: [(block) (layered_texture)]) @definition.constant
(call_expression function: (identifier) @name) @reference.call
