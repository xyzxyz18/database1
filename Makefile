CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
TARGET   := inventory
SRC      := src/inventory.cpp

.PHONY: all run init clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

# 生成一套演示数据集（商品目录 + 进销记录）
init: $(TARGET)
	./$(TARGET) --init

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
