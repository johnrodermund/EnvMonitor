#pragma once

//ph4 and ph7 are the raw ADC readings taken in the pH 4 and pH 7 buffers
inline float phFromAdc(float adc, float ph4, float ph7) {
  float slope = 3 / (ph7 - ph4);
  return (slope * adc) + (4 - (slope * ph4));
}

inline float ecFromAdc(float adc) {
  float voltage = adc / 4095.0 * 3.3;
  return (133.42 * voltage * voltage * voltage - 255.86 * voltage * voltage + 857.39 * voltage) * 0.5;
}
