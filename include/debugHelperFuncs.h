#ifndef DEBUGHELPERFUNCS_H_
#define DEBUGHELPERFUNCS_H_

#if defined(DXCORE)   // Shorthand for cctl v5 board
void serialDebug(){
    Serial.begin(115200);
    Serial1.begin(115200);
    Serial2.begin(115200);
    Serial3.begin(115200);
    Serial4.begin(115200);

    while(true){
        Serial.print("Serial (0) - ");
        Serial1.print("Serial 1 - ");
        Serial2.print("Serial 2 - ");
        Serial3.print("Serial 3 - ");
        Serial4.print("Serial 4 - ");

        Serial.println(millis());
        Serial1.println(millis());
        Serial2.println(millis());
        Serial3.println(millis());
        Serial4.println(millis());

        delay(250);
    }
}
#endif

// #include 
#endif // !DEBUGHELPERFUNCS_H_