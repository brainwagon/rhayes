CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -O2 -g
LDFLAGS = -lm

SRC_DIR = src
OBJ_DIR = obj
INC_DIR = include
LIB_DIR = lib
BIN_DIR = bin

# Object files by library
LIBRH_OBJS = $(OBJ_DIR)/rh_math.o $(OBJ_DIR)/rh_geometry.o $(OBJ_DIR)/rh_shader.o \
             $(OBJ_DIR)/rh_raster.o $(OBJ_DIR)/rh_image.o $(OBJ_DIR)/lodepng.o \
             $(OBJ_DIR)/rh_texture.o $(OBJ_DIR)/rh_shadow.o
LIBRI_OBJS = $(OBJ_DIR)/ri_context.o $(OBJ_DIR)/ri_state.o $(OBJ_DIR)/ri_light.o \
             $(OBJ_DIR)/ri_primitive.o $(OBJ_DIR)/ri_render.o $(OBJ_DIR)/ri_options.o \
             $(OBJ_DIR)/ri_declare.o
LIBRIB_OBJS = $(OBJ_DIR)/rib_output.o
LIBRIBPARSE_OBJS = $(OBJ_DIR)/rib_parse.o

# Libraries
LIBRH = $(LIB_DIR)/librh.a
LIBRI = $(LIB_DIR)/libri.a
LIBRIB = $(LIB_DIR)/librib.a
LIBRIBPARSE = $(LIB_DIR)/libribparse.a

# Programs
TARGET = rhayes
RENDER = $(BIN_DIR)/render
CATRIB = $(BIN_DIR)/catrib
SCENE2RIB = $(BIN_DIR)/scene2rib

.PHONY: all clean libs programs test test-clean generate-refs profile

# Default target - build all executables
all: $(TARGET) $(RENDER) $(CATRIB) $(SCENE2RIB)

# Build all libraries
libs: $(LIBRH) $(LIBRI)

# Build all programs (requires all new files to exist)
programs: $(RENDER) $(CATRIB) $(SCENE2RIB)

# Core rendering library (geometry, shading, rasterization, math, image)
$(LIBRH): $(LIBRH_OBJS) | $(LIB_DIR)
	ar rcs $@ $^

# RenderMan API implementation library (actually renders)
$(LIBRI): $(LIBRI_OBJS) | $(LIB_DIR)
	ar rcs $@ $^

# RIB output library (writes RIB files)
$(LIBRIB): $(LIBRIB_OBJS) | $(LIB_DIR)
	ar rcs $@ $^

# RIB parsing library (reads RIB files)
$(LIBRIBPARSE): $(LIBRIBPARSE_OBJS) | $(LIB_DIR)
	ar rcs $@ $^

# Legacy single executable (for compatibility)
$(TARGET): $(OBJ_DIR)/main.o $(LIBRI) $(LIBRH)
	$(CC) $(OBJ_DIR)/main.o $(LIBRI) $(LIBRH) -o $@ $(LDFLAGS)

# Render program (parse RIB and render)
$(RENDER): $(OBJ_DIR)/render.o $(LIBRIBPARSE) $(LIBRI) $(LIBRH) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

# Catrib program (parse RIB and write RIB)
$(CATRIB): $(OBJ_DIR)/catrib.o $(LIBRIBPARSE) $(LIBRIB) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

# Scene2rib program (output main.c scene as RIB)
$(SCENE2RIB): $(OBJ_DIR)/main_rib.o $(LIBRIB) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

# Compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Create directories
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR) $(BIN_DIR) $(TARGET)

# Test directories
TEST_DIR = tests
TEST_OUT = $(TEST_DIR)/output

# Run all tests
test: $(RENDER) $(CATRIB)
	@chmod +x $(TEST_DIR)/run_tests.sh
	@$(TEST_DIR)/run_tests.sh

# Generate reference images (run once when tests are known-good)
generate-refs: $(RENDER)
	@echo "Generating reference images..."
	@for rib in $$(find $(TEST_DIR)/rib -name "*.rib"); do \
		rel=$${rib#$(TEST_DIR)/rib/}; \
		ref="$(TEST_DIR)/reference/$${rel%.rib}.png"; \
		mkdir -p "$$(dirname "$$ref")"; \
		temp="/tmp/temp_$$(basename $$rib)"; \
		sed "s|Display.*|Display \"$$ref\" \"file\" \"rgba\"|" "$$rib" > "$$temp"; \
		$(RENDER) "$$temp" 2>/dev/null || true; \
		rm -f "$$temp"; \
	done
	@echo "Done."

# Clean test output
test-clean:
	rm -rf $(TEST_OUT)

# Build with profiling enabled (for gprof analysis)
profile: CFLAGS += -pg -fno-omit-frame-pointer
profile: LDFLAGS += -pg
profile: clean all
	@echo "Built with profiling. Run program, then use 'gprof ./rhayes gmon.out' to analyze."
