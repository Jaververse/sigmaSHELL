
CC    := gcc
STD   := -std=gnu11

CFLAGS := $(STD) -Wall -Wextra -g -O2


TARGET := ucvsh

SRCS := main.c      \
        executor.c  \
        parser.c    \
        builtins.c  \
        jobs.c      \
        history.c   \
        input.c

# Generar lista de .o a partir de .c 
# Todos los .o van al directorio build/ para mantener el directorio raíz limpio
BUILD_DIR := build
OBJS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))

.PHONY: all clean rebuild run

# Regla por defecto: compilar el binario
all: $(TARGET)
	@echo ""
	@echo "  ============================================"
	@echo "  ucvsh compilado correctamente."
	@echo "  Ejecutar con: ./ucvsh"
	@echo "  ============================================"
	@echo ""

# --- Enlazado: unir todos los .o en el binario final -------------------------
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# --- Compilación: cada .c → su .o correspondiente en build/ -----------------
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# --- Crear build/ si no existe -----------------------------------------------
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Incluir dependencias de headers auto-generadas (.d)
-include $(OBJS:.o=.d)

# --- Limpiar artefactos ------------------------------------------------------
clean:
	@rm -rf $(BUILD_DIR) $(TARGET)
	@echo "Limpieza completada."

# --- Recompilar desde cero ---------------------------------------------------
rebuild: clean all

# --- Compilar y ejecutar -----------------------------------------------------
run: all
	./$(TARGET)
