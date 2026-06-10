
CC := gcc
STD := -std=gnu11
CFLAGS := $(STD) -Wall -Wextra -g -O2 -MMD -MP
LDFLAGS :=

TARGET := ucvsh
BUILD_DIR := build

SRCS := main.c \
        executor.c \
        parser.c \
        builtins.c \
        jobs.c \
        history.c \
        input.c \
        signKeyboard.c

#generar automaticamente la lista de archivos .o
OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean rebuild run

all: $(TARGET)

	@echo "  ucvsh compilado correctamente."
	@echo "  Ejecutar con: ./ucvsh"


#enlaza todos los objetos del proyecto completo en un unico ejecutable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

#compila cada .c dentro de build/ y genera automaticamente su .d de dependencias
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

#crea el directorio de compilacion si no existe
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

#incluye dependencias de headers sin fallar en la primera compilacion
-include $(DEPS)

#elimina objetos, dependencias y binario final
clean:
	@rm -rf $(BUILD_DIR) $(TARGET)
	@echo "Limpieza completada."

#fuerza una compilacion limpia desde cero
rebuild: clean all

#compila y ejecuta la shell
run: all
	./$(TARGET)
