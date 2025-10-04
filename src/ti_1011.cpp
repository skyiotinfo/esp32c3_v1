#include <LoRa.h>
#include <SPI.h>

int counter = 1;
const int hsen1 = 4;
const int lsen1 = 5;
const int hpin1 = 0;

const int hsen2 = 6;
const int lsen2 = 7;
const int hpin2 = 10;

const int sled = 1;
int value = 11;
int state=0;
int vstate1=2;
int vstate2=2;
int volt_state=1;
int temp_count1=0;

int temp_count2=0;
int temp_count3=0;
int temp_count4=0;
int temp_count5=0;
int temp_count6=0;

int rnum = 3;
int rnum1 = 1;
 
void setup() 
{
  Serial.begin(9600); 
  Serial1.begin(9600, SERIAL_8N1, 3, 2);
  pinMode(hsen1, INPUT_PULLUP); 
  pinMode(lsen1, INPUT_PULLUP); 
  pinMode(hpin1, OUTPUT);

  pinMode(hsen2, INPUT_PULLUP); 
  pinMode(lsen2, INPUT_PULLUP);
  pinMode(hpin2, OUTPUT);

  pinMode(sled, OUTPUT);
  digitalWrite(hpin1,LOW);
  digitalWrite(hpin2,LOW);
  digitalWrite(sled,HIGH);
}

void send_data(){
  int i;
  for(i=0;i<=(10);i++){

  }
  Serial.println("");
}
 
void loop() 
{
  if(digitalRead(hsen1)==0 && digitalRead(hpin1)==1){
    temp_count1=temp_count1+1;
    Serial.println("temp1 count FULL");
    Serial.println(temp_count1);
    if(temp_count1>=3){
      vstate1=0;
    }
    if(temp_count1>=30){
      Serial.println("LOW....");
      digitalWrite(hpin1, LOW);
      temp_count1=0;
    }
  }else{
    temp_count1=0;
  }

  if(digitalRead(lsen1)==0 && digitalRead(hpin1)==0){
    temp_count2=temp_count2+1;
    Serial.println("temp2 count EMPTY");
    Serial.println(temp_count2);
    if(temp_count2>=3){
      Serial.println("HIGH...");
      digitalWrite(hpin1, HIGH);
      vstate1=1;
      //Serial1.println("10230411");
      temp_count2=0;
    }
  }else{
    temp_count2=0;
  }

  if(digitalRead(hsen1)==0 && digitalRead(hpin1)==0){
    vstate1=0;
  }

  if(digitalRead(hpin1)==1 && digitalRead(lsen1)!=0){
    temp_count3=temp_count3+1;
    if(temp_count3>=3){
      state=2;
      vstate1=2;
    }
    if(temp_count3>=30){
      digitalWrite(hpin1, LOW);
      temp_count3=0;
    }
  }else{
    temp_count3=0;
  }

  //=======================

  if(digitalRead(hsen2)==0 && digitalRead(hpin2)==1){
    temp_count4=temp_count4+1;
    Serial.println("temp3 count FULL");
    Serial.println(temp_count3);
    if(temp_count4>=3){
      state=2;
      vstate2=0;
    }
    if(temp_count4>=30){
      Serial.println("LOW....");
      digitalWrite(hpin2, LOW);
      temp_count4=0;
    }
  }else{
    temp_count4=0;
  }

  if(digitalRead(lsen2)==0 && digitalRead(hpin2)==0){
    temp_count5=temp_count5+1;
    Serial.println("temp4 count EMPTY");
    Serial.println(temp_count5);
    if(temp_count5>=3){
      Serial.println("HIGH...");
      digitalWrite(hpin2, HIGH);
      vstate2=1;
      temp_count5=0;
    }
  }else{
    temp_count5=0;
  }

  if(digitalRead(hsen2)==0 && digitalRead(hpin2)==0){
    vstate2=0;
  }
  
  if(digitalRead(hpin2)==1 && digitalRead(lsen2)!=0){
    temp_count6=temp_count6+1;
    if(temp_count6>=3){
      state=2;
      vstate2=2;
    }
    if(temp_count6>=30){
      digitalWrite(hpin2, LOW);
      temp_count6=0;
      
    }
  }else{
    temp_count6=0;
  }

  counter++;
  if(counter>=200){
    counter=10;
  }

  Serial1.print("102304");
  Serial1.print(vstate1);
  Serial1.print(vstate1);
  Serial1.println();
  delay(1000);
  Serial1.flush();
  Serial1.print("102305");
  Serial1.print(vstate2);
  Serial1.print(vstate2);
  Serial1.println();
  digitalWrite(sled,LOW);
  delay(50);
  digitalWrite(sled,HIGH);
  delay(50);
  delay(1000);
}
