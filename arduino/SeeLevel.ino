// The part of my monitor program that reads and decodes the data from the SeeLevel sensors to provide a Fill Percent
// 
// Byte 0: read counter, byte 1: checksum, bytes 2-9: fill data, bytes 10-11: all zeros
// Bytes 2-9 are 8 bit representations of the "fill density" of the 8 sensor segments
//
// 3 tanks x 12 bytes:  
byte SeeLevels[3][12];  //Seelevel raw data storage:
byte lowLimit[3][8];    //May require zero/empty offset for SeeLevel calibration.
byte highLimit[3][8];   //May require zero/empty offset for SeeLevel calibration.
float tankLev[] = {0, 0, 0};      //Initialize tank levels to zero
byte tankArray[] = {90, 35, 25, 0};  //Test values: Fresh, Grey, Black; percent full with last byte reserved for crc
byte tankBaseSeg[] = {9, 6, 6};  //Base segment of bytes 11 (LSB) to 4 (MSB) of sensor strip (Black/Grey are cut down for shallow tank)

// It's important to note that a "full" segment does not mean a reading of 255. Usually a full segment reading only reaches around 125 
// before the next segment starts to register.
// Tank sensors were bench tested to determine variations in the sensor readings along the sensor strips and the following
// parameters were defined to help improve the accuracy of the calculations used to determine the measured tank level
// Use of each is explained further in the related code segments
byte tankFillSeg[] = {0, 0, 0};   //Current top segment of each tank with nonzero reading
float tankSegVol8[] = {18, 13, 11, 15, 11, 11, 11, 10};  //Measured approx. percent fill for each of 8 segments (top to bottom)
float tankSegVol5[] = {22, 21, 20, 19, 18};   //Measured percent fill for each of 5 segments on shallow tanks (top to bottom)
float baseSum;
int byteSum, checkSum;  //SeeLevel data integrity checks
float segVal, fltTFS;

// Main code segment that reads the 3 tank levels. (Lots of left-over print statements for debug of calculations)
    readLevel(tankNum);                // Read Tank levels 0, 1, 2 sequentially. Each Read returns 12 bytes.
    
	for (i = 0; i < 12; i++) {
      Serial.print(SeeLevels[tankNum][i]);  //Store 12 bytes of data in array
      Serial.print(' ');
    }
    Serial.println("----------");
	// Bytes 2 to 9 are an 8-bit code that represents  the fill in each segment of the tank, from bottom (9) to top (2)
    // Fresh tank uses all 8 segments of sensor strip, Grey and Black in my RV use only 5 segs, so decode must be adapted
    // Validate checksum (A checksum is contained in  byte1 (rollover count in byte0 is not used in calculations)
    byteSum = 0;                    
    for (i = 2; i <= 9; i++) {      
      byteSum = byteSum + SeeLevels[tankNum][i];  // Add all 8 data bytes together then ((remainder of byteSum/256) - 2) = checkSum.
    }
    // Verifying the checksum that comes with the SeeLevel data.
    // Remainder of (sum of all 8 data bytes)/256 - (checkSum + 2) should be zero
    checkSum = SeeLevels[tankNum][1];        //Get checksum from 2nd byte of read data
    Serial.print("byteSum % 256 - 2 = ");    
    Serial.print((byteSum % 256) - 2);
    Serial.print(" checkSum = ");
    Serial.println(checkSum);
    // Verify checkSum. Special cases to avoid negative result: if byteSum = 0 or 1, checksum will be 254 or 255 respectively
    if ((byteSum == 0 && checkSum == 254) || (byteSum == 1 && checkSum == 255) ||
        (byteSum % 256) > 0 and (byteSum % 256) == (checkSum + 2)) {                   
    // If data OK:
	// Calculation of total fill is kinda convoluted but it works:
      baseSum = 0;
      fltTFS = 0;
      segVal = 0;
      if (SeeLevels[tankNum][tankBaseSeg[tankNum]] == 0) {        // tankBaseSeg[] is 6 for tank 1 and 2 and 9 for tank 0
        tankFillSeg[tankNum] = tankBaseSeg[tankNum];              // If bottom (base) segment fill = 0, tank is empty so...
        tankLev[tankNum] = 0;
      }
      else {
        i = tankBaseSeg[tankNum];                                 // Data bytes are 2 to 9 or 2 to 6 depending on tank (MSB -> LSB)
        while (SeeLevels[tankNum][i] != 0 && i >= 2) {
          tankFillSeg[tankNum] = (i);                             // tankFillSeg[tankNum] will point to the top non-empty segment of tank[tankNum]
          --i;
        }
        if (tankNum == 0)  {                                      // Fresh water tank (tank 0) has 8 segment sensor
          for (i = 7; i > (tankFillSeg[tankNum] - 2); i--) {      // Add together the percent fill of each segment but the top one
            baseSum = baseSum + tankSegVol8[i];
          }
        }
        else {                                                    // Gray and Black tanks (tanks 1 and 2) have 5 segment sensorS
          for (i = 4; i > (tankFillSeg[tankNum] - 2); i--) {      // Add together the percent fill of each segment but the top one
            baseSum = baseSum + tankSegVol5[i];
          }
        }                                                          // Tank 0 (Fresh)has 8 segment sensor
        if (tankNum == 0) {                                        // Calculate the percent fill of the top non-zero segment(Fresh tank)
          fltTFS = SeeLevels[tankNum][tankFillSeg[tankNum]];       // convert partial tankFill to float to compare with typicsl "full" value
          segVal = (fltTFS / 125) * tankSegVol8[tankFillSeg[tankNum] - 2];    // 125 chosen to give close match to actual SeeLevel gauge
        }                                                          // Tanks 1 and 2(Grey and Black)have 5 segment sensors
        else {                                                     // Calculate the percent fill of the top non-zero segment(gray & black tanks)
          fltTFS = SeeLevels[tankNum][tankFillSeg[tankNum]];       // convert to float
          segVal = (fltTFS / 125) * tankSegVol5[tankFillSeg[tankNum] - 2];
        }
        tankLev[tankNum] = baseSum + segVal;                       // Add base fill and topseg fill together for total calculated tank fill
      }
	        //Serial.println(" ");
      Serial.print("BaseSeg = ");
      Serial.print(tankBaseSeg[tankNum]);
      Serial.print(" FillSeg = ");
      Serial.print(tankFillSeg[tankNum]);
      Serial.print(" FillSeg Level = ");
      Serial.print(SeeLevels[tankNum][tankFillSeg[tankNum]]);
      if (tankNum == 0) {
        Serial.print(" tankSegVol8 = ");
        Serial.println(tankSegVol8[tankFillSeg[tankNum] - 2]);
        //Serial.print(" tankSegVol8 = ");
        //Serial.println(tankSegVol8[tankFillSeg[tankNum] - 2]);
      }
      else {
        Serial.print(" tankSegVol5 = ");
        Serial.println(tankSegVol5[tankFillSeg[tankNum] - 2]);
      }
      Serial.print("baseSum = ");
      Serial.print(baseSum);
      Serial.print(" fltTFS = ");
      Serial.print(fltTFS);
      Serial.print(" segVal = ");
      Serial.print(segVal);

      tankLev[tankNum] = round(tankLev[tankNum]);
      if (tankLev[tankNum] > 100) tankLev[tankNum] = 100;  //Don't let tankLev exceed 100%
      Serial.print(" tankLev = ");
      Serial.println(tankLev[tankNum]);
      Serial.println("--------");

      //    Save limit values of 8 data bytes per sample for transfer to Mega
      //    Serial.print(SeeLevels[tankNum][0]);
      //    Serial.print(" ");
      //    Serial.print(SeeLevels[tankNum][1]);
      //Serial.println(" ");
      for (i = 2; i < 10; i++) {                     // Save and Print read values for verification
        if (SeeLevels[tankNum][i] > highLimit[tankNum][i - 2]) {
          highLimit[tankNum][i - 2] = SeeLevels[tankNum][i];
        }
        if (SeeLevels[tankNum][i] < lowLimit[tankNum][i - 2]) {
          lowLimit[tankNum][i - 2] = SeeLevels[tankNum][i];
        }
      }
	  
	  //Potential use of a zero/empty offset to provide better accuracy if tanks don't read empty when they are in fact empty
      Serial.println("Low-High Limit Check: ");
      for (i = 0; i < 8; i++) {
        Serial.print(lowLimit[tankNum][i]);
        Serial.print(" ");
        Serial.print(SeeLevels[tankNum][i + 2], HEX);
        Serial.print(" ");
        Serial.println(highLimit[tankNum][i]);
      }

      Serial.println(" ");

      
     
    }
    else {
      tankErr[tankNum] = ++tankErr[tankNum];      // increment error count for this tank
      dataUpdate = true;
    }
    if (tankNum != 2) {               // increment or reset tank number
      ++tankNum;
    }
    else {
      tankNum = 0;
    }
    tankPass = 1;
    //delay(200);

  


// Read individual tank levels
void readLevel(int t) {              // passed variable (t) is 0, 1 or 2          //Could use time check instead iof delay?
  digitalWrite(pinSLout, HIGH);     // Power the sensor line for 2.4 ms so tank levels can be read
  delayMicroseconds(2450);
  for (i = 0; i <= t; i++) {        // 1, 2 or 3 low pulses to select Fresh, Grey, Black tank
    digitalWrite(pinSLout, LOW);    // See RV Panel data file for protocol details
    delayMicroseconds(85);          // These settings give the 85 down/300 up (microsecond) pulse
    digitalWrite(pinSLout, HIGH);
    delayMicroseconds(290);
  }
  for (byteLoop = 0; byteLoop < 12; byteLoop++) {
    SeeLevels[t][byteLoop] = ~readByte();  // Populate 12 bytes with bitwise inverted readings (FF > 00)
  }
  delay(20);                    // Leave power on long enough to allow sensor to transmit data stream
  digitalWrite(pinSLout, LOW);  //Turn power off until next poll
}

byte readByte() {               //Function to read individual bytes from SeeLevel sensor
  result = 0;
  for (bitLoop = 0; bitLoop < 8; bitLoop++) {   // We need to populate byte from right to left,
    result = result << 1;                       // so shift right for each incoming bit
    pulseTime = (pulseIn(pinSLin, LOW));        // If needed, pulseIn() has optional 3rd arg: "timeout" in microseconds
    if (pulseTime  <= 26) {                     // "0" bit is low for about 43 microseconds, a "1" for about 13 microseconds
      thisBit = 1;                              // so 26 is the close to the decision point (tune for error improvement)
    }
    else  {
      thisBit = 0;
    }
    result |= thisBit;                          // compound bitwise OR succesive reads into shifted single byte
  }
  return result;
}

 