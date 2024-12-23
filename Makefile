# Define the C++ compiler
CXX = g++

# Define the C++ compiler flags
CXXFLAGS = -std=c++17 -Wall -Wextra

# Define the target executable
TARGET = rs-cache-finder-linux

# Define the source file
SOURCE = rs-cache-finder-linux.cpp

# Default target
all: $(TARGET)

# Compile the source file into an executable
$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE)

# Clean target
clean:
	rm -f $(TARGET)
