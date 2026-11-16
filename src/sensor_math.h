#pragma once

//ph4 and ph7 are the raw ADC readings taken in the pH 4 and pH 7 buffers
inline float phFromAdc(float adc, float ph4, float ph7) {
  float slope = 3 / (ph7 - ph4);
  return (slope * adc) + (4 - (slope * ph4));
}

//EC referred to 25 C: EC25 = EC / (1 + coef/100 * (T - 25)), T in deg C from the DS18B20's deg F.
//coef is % per deg C; 0 turns compensation off. A missing or implausible temperature (outside 0-50 C,
//which includes the DS18B20's -127 C "disconnected" and 85 C power-on values) leaves EC raw.
inline float ecAt25(float ec, float tempF, float coefPercent) {
  float tempC = (tempF - 32) * 5 / 9;
  if (coefPercent == 0 || !(tempC >= 0 && tempC <= 50)) {
    return ec;
  }
  return ec / (1 + coefPercent / 100 * (tempC - 25));
}

inline float ecFromAdc(float adc) {
  float voltage = adc / 4095.0 * 3.3;
  return (133.42 * voltage * voltage * voltage - 255.86 * voltage * voltage + 857.39 * voltage) * 0.5;
}
