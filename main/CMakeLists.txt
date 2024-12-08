# Embed the server root certificate into the final binary
#
# (If this was a component, we would set COMPONENT_EMBED_TXTFILES here.)
idf_component_register(SRCS "main.c" "wifi_connect.c" "time_sync.c" "tg.c" "handler.c" "gate_control.c"
                    INCLUDE_DIRS "include" "../lib/jsmn")
