cmake_policy(SET CMP0053 NEW)

set("semicolon;in;name" semicolon)
set("dollar$in$name" dollar)
set("brace{in}name" brace)
set("bracket[in]name" bracket)
set("newline\nin\nname" newline)
set("octothorpe\#in\#name" octothorpe)

message("-->${semicolon\;in\;name}<--")
message("-->${dollar\$in\$name}<--")
message("-->${brace\{in\}name}<--")
message("-->${bracket\[in\]name}<--")
message("-->${newline\nin\nname}<--")
message("-->${octothorpe\#in\#name}<--")

message(-->top-level;semicolon<--)
message(-->top-level\;escaped\;semicolon<--)
