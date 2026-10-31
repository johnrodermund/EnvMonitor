#pragma once

template <int capacity>
class RingStats {

public:

  float arr[capacity];
  int index = 0;
  float mean = 0;
  float varSum = 0;
  float sVar = 0;
  float currentValue = 0;
  bool full = false;

  int size() {
    return full ? capacity : index;
  }

  void updatedata(float new_value) {
    currentValue = new_value;
    if (!full) {
      arr[index] = new_value;
      index++;
      float old_delta = new_value - mean;
      mean += old_delta / index;
      float new_delta = new_value - mean;
      varSum += old_delta * new_delta;
      sVar = index > 1 ? varSum / (index - 1) : 0;
      if (index == capacity) {
        full = true;
        index = 0;
      }
    } else {
      float old_value = arr[index];
      arr[index] = new_value;
      float new_mean = mean + ((new_value - old_value) / capacity);
      varSum += (old_value + new_value - mean - new_mean) * (new_value - old_value);
      sVar = varSum / (capacity - 1);
      mean = new_mean;
      index = (index + 1) % capacity;
    }
  }

  float getOrderedValue(int count) {
    if (!full) {
      return arr[count];
    }
    return arr[(index + count) % capacity];
  }

  void resetDataset() {
    index = 0;
    mean = 0;
    varSum = 0;
    sVar = 0;
    full = false;
    currentValue = 0;
  }
};
