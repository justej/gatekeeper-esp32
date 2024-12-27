# Embed the server root certificate into the final binary
#
# (If this was a component, we would set COMPONENT_EMBED_TXTFILES here.)
idf_component_register(SRCS "main.c" "wifi_connect.c" "gate_control.c" "time_sync.c" "users.c" "tg/tg.c" "tg/handler.c"
                    INCLUDE_DIRS "include" "../lib/jsmn")
