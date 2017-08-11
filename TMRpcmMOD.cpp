/* Library by TMRh20 2012-2014
*  CKD - Pham Duy Anh modify TMRpcm library
*  2017-06-25	Add more comment
*  2017-06-27	Short Cut to use only Arduino UNO or ATmega328
*				- del MODE2
*				- del DISABLE_SPEAKER2, always use two output 9 & 10
*				- del STEREO_OR_16BIT
*				- del USE_TIMER2, always use TIMER1 16bit
*				- del rampMega
*				- del ENABLE_MULTI
*/

#include <pcmConfigMOD.h>
#if !defined (SDFAT)
	#include <SD.h>
#else
	#include <SdFat.h>
#endif
#include <TMRpcmMOD.h>

#if !defined (RF_ONLY)

//********************* Timer arrays and pointers **********************
//********** Enables use of different timers on different boards********

const byte togByte = _BV(ICIE1); //Get the value for toggling the buffer interrupt on/off

#if !defined (buffSize)
	#define buffSize 64
#endif

//*********** Standard Global Variables ***************
volatile unsigned int dataEnd;
volatile boolean buffEmpty[2] = {true,true}, whichBuff = false, playing = 0, a, b;

	//*** Options/Indicators from MSb to LSb: paused, qual, rampUp, 2-byte samples, loop, loop2nd track, 16-bit ***
byte optionByte = B01100000;

volatile byte buffer[2][buffSize], buffCount = 0;
char volMod=0;

volatile boolean loadCounter=0;

#if !defined (SDFAT)
	File sFile;
#else
	SdFile sFile;
#endif

#if defined (ENABLE_RECORDING)
	Sd2Card card1;
	byte recording = 0;
#endif

//**************************************************************
//********** Core Playback Functions used in all modes *********

void TMRpcm::timerSt()
{
	ICR1 = resolution;
	// Stereo
	TCCR1A = _BV(WGM11) | _BV(COM1A1) | _BV(COM1B1); //WGM11,12,13 all set to 1 = fast PWM/w ICR TOP
	// 16 bit
	//TCCR1A = _BV(WGM11) | _BV(COM1A1) | _BV(COM1B0) | _BV(COM1B1); //WGM11,12,13 all set to 1 = fast PWM/w ICR TOP
	TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
}

void TMRpcm::setPin()
{
	disable();
	//pinMode(speakerPin,OUTPUT);
	pinMode(9,  OUTPUT);
	pinMode(10, OUTPUT);

	#if defined (SD_FULLSPEED)
		SPSR |= (1 << SPI2X);
		SPCR &= ~((1 <<SPR1) | (1 << SPR0));
	#endif
}

boolean TMRpcm::wavInfo(char* filename)
{
	//check for the string WAVE starting at 8th bit in header of file
	#if !defined (SDFAT)
		sFile = SD.open(filename);
	#else
		sFile.open(filename);
	#endif

	if( !ifOpen() )
	{
		return 0;
	}

	seek(8);
	char wavStr[] = {'W','A','V','E'};
	for (byte i =0; i<4; i++)
	{
		if(sFile.read() != wavStr[i])
		{
			#if defined (debug)
				Serial.println("WAV ERROR");
			#endif
			break;
		}
	}

	byte stereo, bps;
	seek(22);
	stereo = sFile.read();
	seek(24);

	SAMPLE_RATE = sFile.read();
	SAMPLE_RATE = sFile.read() << 8 | SAMPLE_RATE;

	//verify that Bits Per Sample is 8 (0-255)
	seek(34);
	bps = sFile.read();
	bps = sFile.read() << 8 | bps;

	if( stereo == 2)
	{ //_2bytes=1;
		bitSet(optionByte,4);
	}
	else if( bps == 16 )
	{
		bitSet(optionByte,1);
		bitSet(optionByte,4);
		// invert output B for output 16bit
		TCCR1A = _BV(WGM11) | _BV(COM1A1) | _BV(COM1B0) | _BV(COM1B1);
	}
	else
	{
		bitClear(optionByte,4);
		bitClear(optionByte,1);
	}

	#if defined (HANDLE_TAGS)
		seek(36);
		char datStr[4] = {'d','a','t','a'};
		for (byte i =0; i<4; i++)
		{
			if(sFile.read() != datStr[i])
			{
				seek(40);
				unsigned int siz = sFile.read();
				siz = (sFile.read() << 8 | siz)+2;
				seek(fPosition() + siz);
				for (byte i =0; i<4; i++)
				{
					if(sFile.read() != datStr[i])
					{
						return 0;
					}
				}
			}
		}

		unsigned long dataBytes = sFile.read();
		for (byte i =8; i<32; i+=8)
		{
			dataBytes = sFile.read() << i | dataBytes;
		}
		#if !defined (SDFAT)
			dataEnd = sFile.size() - fPosition() - dataBytes + buffSize;
		#else
			dataEnd = sFile.fileSize() - fPosition() - dataBytes + buffSize;
		#endif
	#else //No Tag handling
		seek(44); dataEnd = buffSize;
	#endif

	return 1;
}

//*************** General Playback Functions *****************
void TMRpcm::quality(boolean q)
{
	if(!playing)
	{
		bitWrite(optionByte,6,q);
	} //qual = q; }
}

void TMRpcm::stopPlayback()
{
	playing = 0;

	TIMSK1 &= ~(togByte | _BV(TOIE1));

	if(ifOpen())
	{
		sFile.close();
	}
}

void TMRpcm::pause()
{
	//paused = !paused;
	if(bitRead(optionByte,7) && playing)
	{
		bitClear(optionByte,7);
		TIMSK1 |= ( _BV(ICIE1) | _BV(TOIE1) );
	}
	else if(!bitRead(optionByte,7) && playing)
	{
		bitSet(optionByte,7);
		TIMSK1 &= ~( _BV(TOIE1) );
	}
}

void TMRpcm::loop(boolean set)
{
	bitWrite(optionByte,3,set);
}

/**************************************************
This section used for translation of functions between
 SDFAT library and regular SD library
Prevents a whole lot more #if defined statements */

#if !defined (SDFAT)
	boolean TMRpcm::seek( unsigned long pos )
	{
		return sFile.seek(pos);
	}

	unsigned long TMRpcm::fPosition( )
	{
		return sFile.position();
	}

	boolean TMRpcm::ifOpen()
	{
		if(sFile){ return 1;}
	}
#else
	boolean TMRpcm::seek(unsigned long pos )
	{
		return sFile.seekSet(pos);
	}

	unsigned long TMRpcm::fPosition()
	{
		return sFile.curPosition();
	}

	boolean TMRpcm::ifOpen()
	{
		return sFile.isOpen();
	}
#endif

//***************************************************************************************
//********************** Functions for single track playback ****************************
void TMRpcm::play(char* filename)
{
	play(filename,0);
}

void TMRpcm::play(char* filename, unsigned long seekPoint)
{
	stopPlayback();

	if(!wavInfo(filename)) //verify its a valid wav file
	{
		#if defined (debug)
			Serial.println("WAV ERROR");
		#endif
		return;
	}

	if(seekPoint > 0)
	{
		seekPoint = (SAMPLE_RATE*seekPoint) + fPosition();
		seek(seekPoint); //skip the header info
	}

	playing = 1; bitClear(optionByte,7); //paused = 0;

	if(bitRead(optionByte,6))
	{
		resolution = 10 * (800000/SAMPLE_RATE);
	}
	else
	{
		resolution = 10 * (1600000/SAMPLE_RATE);
	}

	byte tmp = (sFile.read() + sFile.peek()) / 2;

	unsigned int mod;
	if(volMod > 0)
	{
		mod = OCR1A >> volMod;
	}
	else
	{
		mod = OCR1A << (volMod*-1);
	}

	if(tmp > mod)
	{
		for(unsigned int i=0; i<buffSize; i++)
		{
			mod = constrain(mod+1,mod, tmp);
			buffer[0][i] = mod;
		}

		for(unsigned int i=0; i<buffSize; i++)
		{
			mod = constrain(mod+1,mod, tmp);
			buffer[1][i] = mod;
		}
	}
	else
	{
		for(unsigned int i=0; i<buffSize; i++)
		{
			mod = constrain(mod-1,tmp ,mod);
			buffer[0][i] = mod;
		}

		for(unsigned int i=0; i<buffSize; i++)
		{
			mod = constrain(mod-1,tmp, mod);
			buffer[1][i] = mod;
		}
	}

	whichBuff = 0;
	buffEmpty[0] = 0;
	buffEmpty[1] = 0;
	buffCount = 0;

	noInterrupts();
	timerSt();
	TIMSK1 = ( togByte | _BV(TOIE1) );

	interrupts();
}

void TMRpcm::volume(char upDown)
{
	if(upDown)	{ volMod++; }
	else 		{ volMod--; }
}

void TMRpcm::setVolume(char vol)
{
	volMod = vol - 4 ;
}

#if defined (ENABLE_RECORDING)
	ISR(TIMER1_COMPA_vect)
	{
		if(buffEmpty[!whichBuff] == 0)
		{
			a = !whichBuff;
			TIMSK1 &= ~(_BV(OCIE1A));
			sei();
			sFile.write((byte*)buffer[a], buffSize );
			buffEmpty[a] = 1;
			TIMSK1 |= _BV(OCIE1A);
		}
	}
#endif

ISR(TIMER1_CAPT_vect)
{
	// The first step is to disable this interrupt before manually enabling global interrupts.
	// This allows this interrupt vector (COMPB) to continue loading data while allowing the overflow interrupt
	// to interrupt it. ( Nested Interrupts )
	// TIMSK1 &= ~_BV(ICIE1);
	//Then enable global interupts before this interrupt is finished, so the music can interrupt the buffering
	//sei();

	if(buffEmpty[!whichBuff])
	{
		a = !whichBuff;
		TIMSK1 &= ~togByte;
		sei();

		if( sFile.available() <= dataEnd)
		{
			#if !defined (SDFAT)
				if(bitRead(optionByte,3))
				{
					sFile.seek(44);
					TIMSK1 |= togByte;
					return;
				}

				TIMSK1 &= ~( togByte | _BV(TOIE1) );
				if(sFile)
				{
					sFile.close();
				}
			#else
				if(bitRead(optionByte,3))
				{
					sFile.seekSet(44);
					TIMSK1 |= togByte;
					return;
				}
				TIMSK1 &= ~( togByte | _BV(TOIE1) );
				if(sFile.isOpen())
				{
					sFile.close();
				}
			#endif
			playing = 0;
			return;
	  	}
	  	sFile.read((byte*)buffer[a],buffSize);
	  	buffEmpty[a] = 0;
	  	TIMSK1 |= togByte;
   	}
}

#if defined (ENABLE_RECORDING)
	ISR(TIMER1_COMPB_vect)
	{
		buffer[whichBuff][buffCount] = ADCH;
		if(recording > 1)
		{
			if(volMod < 0 ){  OCR1A = ADCH >> (volMod*-1); }
			else 		   {  OCR1A = ADCH << volMod; }
		}
		buffCount++;
		if(buffCount >= buffSize)
		{
			buffCount = 0;
			buffEmpty[!whichBuff] = 0;
			whichBuff = !whichBuff;
		}
	}
#endif

ISR(TIMER1_OVF_vect)
{
	if(bitRead(optionByte,6))
	{
		loadCounter = !loadCounter;
		if(loadCounter){ return; }
	}

	if( !bitRead(optionByte,4) )
	{
		if(volMod < 0 ){  OCR1A = OCR1B = buffer[whichBuff][buffCount] >> (volMod*-1); }
		else 		   {  OCR1A = OCR1B = buffer[whichBuff][buffCount] << volMod; }

		++buffCount;
	}
	else
	{
		if(bitRead(optionByte,1))
		{
			buffer[whichBuff][buffCount] += 127;
			//buffer[whichBuff][buffCount+1] += 127;
		}

		if(volMod < 0 )
		{
			OCR1A = buffer[whichBuff][buffCount] >> (volMod*-1);
			OCR1B = buffer[whichBuff][buffCount+1] >> (volMod*-1);
		}
		else
		{
			OCR1A = buffer[whichBuff][buffCount] << volMod;
			OCR1B = buffer[whichBuff][buffCount+1] << volMod;
		}

		buffCount+=2;
	}

	if(buffCount >= buffSize)
	{
		buffCount = 0;
		buffEmpty[whichBuff] = true;
		whichBuff = !whichBuff;
	}
}

void TMRpcm::disable()
{
	playing = 0;
	TIMSK1 &= ~( togByte | _BV(TOIE1) );
	if(ifOpen()){ sFile.close();}
	if(bitRead(TCCR1A,7) > 0)
	{
		int current = OCR1A;
		for(int i=0; i < resolution; i++)
		{
			OCR1B = constrain((current - i),0,resolution);
			OCR1A = constrain((current - i),0,resolution);
			for(int i=0; i<10; i++)
			{
				while(TCNT1 < resolution-50){}
			}
		}
	}
    bitSet(optionByte,5);
    TCCR1A = TCCR1B = 0;
}

boolean TMRpcm::isPlaying()
{
	return playing;
}



#if defined (HANDLE_TAGS)
	//****************** Metadata Features ****************************
	//****************** ID3 and LIST Tags ****************************

	byte TMRpcm::getInfo(char* filename, char* tagData, byte infoNum)
	{
		byte gotInfo = 0;
		if( (gotInfo = metaInfo(1,filename, tagData, infoNum)) < 1)
		{
			gotInfo = metaInfo(0,filename, tagData, infoNum);
		}
		return gotInfo;
	}

	byte TMRpcm::listInfo(char* filename, char* tagData, byte infoNum)
	{
		return metaInfo(0, filename, tagData, infoNum);
	}

	//http://id3.org/id3v2.3.0
	byte TMRpcm::id3Info(char* filename, char* tagData, byte infoNum)
	{
		return metaInfo(1, filename, tagData, infoNum);
	}
	byte TMRpcm::metaInfo(boolean infoType, char* filename, char* tagData, byte whichInfo)
	{
		if(ifOpen()){ noInterrupts();}

		#if !defined (SDFAT)
			File xFile;
			xFile = SD.open(filename);
			xFile.seek(36);
		#else
			SdFile xFile;
			xFile.open(filename);
			xFile.seekSet(36);
		#endif

		boolean found=0;
		char* datStr = "LIST";
		if(infoType == 1){datStr = "ID3 "; datStr[3] = 3;}
		char tmpChars[4];

		if(infoType == 0)
		{ //if requesting LIST info, check for data at beginning of file first
			xFile.read((char*)tmpChars,4);
			for (byte i=0; i<4; i++)
			{ //4 tagSize
				if(tmpChars[i] != datStr[i]) { break; }
				else if(i==3)				 { found = 1; }
			}
		}
		if(found == 0)
		{
			#if !defined (SDFAT)
				found = searchMainTags(xFile, datStr);
			#else
				unsigned long pos = searchMainTags(xFile, datStr);
				xFile.seekSet(pos);
				if(pos > 0){ found = 1; }
			#endif
		}

		//** This section finds the starting point and length of the tag info
		if(found == 0){ xFile.close(); if(ifOpen()){ interrupts();} return 0; }

		unsigned long listEnd;
		unsigned int listLen;
		char* tagNames[] = {"INAM","IART","IPRD"};

		if(infoType == 0)
		{ //LIST format
			listLen = xFile.read(); listLen = xFile.read() << 8 | listLen;
			#if !defined (SDFAT)
				xFile.seek(xFile.position() +6);
				listEnd = xFile.position() + listLen;
			#else
				xFile.seekSet(xFile.curPosition() +6);
				listEnd = xFile.curPosition() + listLen;
			#endif
		}
		else
		{				//ID3 format
			#if !defined (SDFAT)
				xFile.seek(xFile.position() + 5);
			#else
				xFile.seekSet(xFile.curPosition() + 5);
			#endif
				listLen = xFile.read() << 7 | listLen; listLen = xFile.read() | listLen;
				tagNames[0] = "TPE1"; tagNames[1] ="TIT2"; tagNames[2] ="TALB";
			#if !defined (SDFAT)
				listEnd = xFile.position() + listLen;
			#else
				listEnd = xFile.curPosition() + listLen;
			#endif
		}

		char tgs[4];
		unsigned int len = 0;
		unsigned long tagPos = 0;

		//** This section reads the tags and gets the size of the tag data and its position
		//** Should work with very long tags if a big enough buffer is provided
		#if !defined (SDFAT)
			while(xFile.position() < listEnd)
			{
		#endif
		#if defined (SDFAT)
			while(xFile.curPosition() < listEnd)
			{
		#endif
				xFile.read((char*)tgs,4);

				if(infoType == 0)
				{ //LIST
					len = xFile.read()-1;
					len = xFile.read() << 8 | len;
					#if !defined (SDFAT)
						xFile.seek(xFile.position()+2);
					#else
						xFile.seekSet(xFile.curPosition()+2);
					#endif
				}
				else
				{ 				//ID3
					#if !defined (SDFAT)
						xFile.seek(xFile.position()+3);
					#else
						xFile.seekSet(xFile.curPosition()+3);
					#endif
					len = xFile.read();
					len = xFile.read() << 8 | len;
					len = (len-3)/2;
					#if !defined (SDFAT)
						tagPos = xFile.position() + 4;
						xFile.seek(tagPos);
					#else
					    xFile.seekSet(xFile.curPosition() +4);
						tagPos = xFile.curPosition();
					#endif
				}
				found =0;
			//** This section checks to see if the tag we found is the one requested
			//** If so, it loads the data into the buffer
				for(int p=0; p<4;p++)
				{
					if(tgs[p] != tagNames[whichInfo][p]) { break; }
					else
					{
						if(p==3)
						{
							if(infoType == 1)
							{
								byte junk;
								for(byte j=0; j<len; j++)
								{
									tagData[j] = xFile.read();
									junk=xFile.read();
								}
							}
							else
							{
								xFile.read((char*)tagData,len);
							}
							tagData[len] = '\0';
							xFile.close();
							if(ifOpen()){ interrupts();}
		 					return len;
						}
					}
				}

				if(found){break;}

				//**This section jumps to the next tag position if the requested tag wasn't found
				#if !defined (SDFAT)
					if(infoType == 0)
					{
						if(!found){	xFile.seek(xFile.position()+len);}
						while(xFile.peek() == 0){xFile.read();}
					}
					else
					{
						if(!found){	xFile.seek(tagPos+len); }
						while(xFile.peek() != 'T'){xFile.read();}
					}
				#else
					if(infoType == 0)
					{
						if(!found){	xFile.seekSet(xFile.curPosition()+len);}
						while(xFile.peek() == 0){xFile.read();}
					}
					else
					{
						if(!found){	xFile.seekSet(tagPos+len); }
						while(xFile.peek() != 'T'){xFile.read();}
					}
				#endif
			}
			xFile.close();
			if(ifOpen()){ interrupts();}
			return 0;
	}

	#if !defined (SDFAT)
		boolean TMRpcm::searchMainTags(File xFile, char *datStr)
		{
			xFile.seek(36);
	#else
		unsigned long TMRpcm::searchMainTags(SdFile xFile, char *datStr)
		{
			xFile.seekSet(36);
	#endif
			boolean found = 0;
			char dChars[4] = {'d','a','t','a'};
			char tmpChars[4];

			//xFile.seek(36);
			xFile.read((char*)tmpChars,4);
			for (byte i =0; i<4; i++)
			{
				if(tmpChars[i] != dChars[i])
				{
					#if !defined (SDFAT)
						xFile.seek(40);
						unsigned int siz = xFile.read(); siz = (xFile.read() << 8 | siz)+2;
						xFile.seek(xFile.position() + siz);
					#else
						xFile.seekSet(40);
						unsigned int siz = xFile.read(); siz = (xFile.read() << 8 | siz)+2;
						xFile.seekSet(xFile.curPosition() + siz);
					#endif
					xFile.read((char*)tmpChars,4);
					for (byte i =0; i<4; i++)
					{
						if(tmpChars[i] != dChars[i]) { return 0; }
					}
				}
			}

			unsigned long tmpp=0;
			unsigned long daBytes = xFile.read();
			for (byte i =8; i<32; i+=8)
			{
				tmpp = xFile.read();
				daBytes = tmpp << i | daBytes;
			}
			#if !defined (SDFAT)
				daBytes = xFile.position() + daBytes;
				if(xFile.size() == daBytes){ return 0; }
			#else
				daBytes = xFile.curPosition() + daBytes;
				if(xFile.fileSize() == daBytes){ return 0; }
			#endif

			//if(found == 0){ //Jump to file end - 1000 and search for ID3 or LIST
			#if !defined (SDFAT)
				xFile.seek(daBytes);
			#else
				xFile.seekSet(daBytes);
			#endif

			while(xFile.available() > 5)
			{
				if(xFile.read() == datStr[0] && xFile.peek() == datStr[1])
				{
					xFile.read((char*)tmpChars,3);
					if( tmpChars[1] == datStr[2] &&  tmpChars[2] == datStr[3] )
					{
						found = 1;
						#if !defined (SDFAT)
							return 1; break;
						#else
							return xFile.curPosition();
						#endif
					}
					else
					{
						#if !defined (SDFAT)
							xFile.seek(xFile.position() - 1 - 4); //pos - tagSize
						#else
							unsigned long pos = xFile.curPosition()-1;
							xFile.seekSet(pos - 4);
						#endif
					}
				}
			}
			return 0;
		}
#endif

// CKD
#if defined (ENABLE_RECORDING)
	/*********************************************************************************
	********************** DIY Digital Audio Generation ******************************/

	void TMRpcm::finalizeWavTemplate(char* filename){
		disable();

		unsigned long fSize = 0;

	  #if !defined (SDFAT)
			sFile = SD.open(filename,FILE_WRITE);

	    if(!sFile){
			#if defined (debug)
				Serial.println("fl");
			#endif
			return;
		}
		fSize = sFile.size()-8;

	  #else
			sFile.open(filename,O_WRITE );

	    if(!sFile.isOpen()){
			#if defined (debug)
				Serial.println("failed to finalize");
			#endif
			return; }
	    fSize = sFile.fileSize()-8;

	  #endif



		seek(4); byte data[4] = {lowByte(fSize),highByte(fSize), fSize >> 16,fSize >> 24};
		sFile.write(data,4);
		byte tmp;
		seek(40);
		fSize = fSize - 36;
		data[0] = lowByte(fSize); data[1]=highByte(fSize);data[2]=fSize >> 16;data[3]=fSize >> 24;
		sFile.write((byte*)data,4);
		sFile.close();

		#if defined (debug)
		#if !defined (SDFAT)
			sFile = SD.open(filename);
		#else
			sFile.open(filename);
		#endif

		if(ifOpen()){

			    while (fPosition() < 44) {
			      Serial.print(sFile.read(),HEX);
			      Serial.print(":");
			   	}
			   	Serial.println("");

		   	//Serial.println(sFile.size());
	    	sFile.close();
		}
		#endif
	}



	void TMRpcm::createWavTemplate(char* filename, unsigned int sampleRate){
		disable();

	  #if !defined (SDFAT)
	  	if(SD.exists(filename)){SD.remove(filename);}
	  #endif

	  #if defined (ENABLE_RECORDING)
	    SdVolume vol;
		SdFile rut;
		SdFile fil;

		char* fNam = filename;
		uint32_t bgnBlock, endBlock;

		if (!card1.init(SPI_FULL_SPEED,CSPin)) {
		    return;
	  	}else{}//Serial.println("SD OK");}

		if(!vol.init(&card1)){}//Serial.println("card failed"); }
		if (!rut.openRoot(&vol)) {}//Serial.println("openRoot failed"); }
		SdFile::remove(&rut, fNam);

		if (!fil.createContiguous(&rut, fNam, 512UL*BLOCK_COUNT)) {
		    //Serial.println("createContiguous failed");
		  }
		  // get the location of the file's blocks
		  if (!fil.contiguousRange(&bgnBlock, &endBlock)) {
		    //Serial.println("contiguousRange failed");
		  }
		  if (!card1.erase(bgnBlock, endBlock)) //Serial.println("card.erase failed");

		rut.close();
		fil.close();
	#endif

	  #if !defined (SDFAT)
	  		if(SD.exists(filename)){SD.remove(filename);}
			sFile = SD.open(filename,FILE_WRITE);
		if(!sFile){
			#if defined (debug)
				Serial.println("failed to open 4 writing");
			#endif
			return;
		}else{

	  #else
	   	sFile.open(filename,O_CREAT | O_WRITE);
	   	if(sFile.fileSize() > 44 ){ sFile.truncate(44);}
		if(!sFile.isOpen()){
			#if defined (debug)
				Serial.println("failed to open 4 writing");
			#endif
			return;
		}else{

	  #endif
	  		//Serial.print("Sr: ");
	  		//Serial.println(sampleRate);
	  		seek(0);
			byte data[] = {16,0,0,0,1,0,1,0,lowByte(sampleRate),highByte(sampleRate)};
			sFile.write((byte*)"RIFF    WAVEfmt ",16);
			sFile.write((byte*)data,10);
			//unsigned int byteRate = (sampleRate/8)*monoStereo*8;
			//byte blockAlign = monoStereo * (bps/8); //monoStereo*(bps/8)
			data[0] = 0; data[1] = 0; data[2] = lowByte(sampleRate); data[3] =highByte(sampleRate);//Should be byteRate
			data[4]=0;data[5]=0;data[6]=1; //BlockAlign
			data[7]=0;data[8]=8;data[9]=0;
			sFile.write((byte*)data,10);
			sFile.write((byte*)"data    ",8);
			//Serial.print("siz");
			//Serial.println(sFile.size());
			sFile.close();

		}
	}



	void TMRpcm::startRecording(char *fileName, unsigned int SAMPLE_RATE, byte pin){
		startRecording(fileName,SAMPLE_RATE,pin,0);
	}

	void TMRpcm::startRecording(char *fileName, unsigned int SAMPLE_RATE, byte pin, byte passThrough){

		recording = passThrough + 1;
		setPin();
		if(recording < 3){
			//*** Creates a blank WAV template file. Data can be written starting at the 45th byte ***
			createWavTemplate(fileName, SAMPLE_RATE);

			//*** Open the file and seek to the 44th byte ***
		  #if !defined (SDFAT)
			sFile = SD.open(fileName,FILE_WRITE);
			if(!sFile){
				#if defined (debug)
					Serial.println("fail");
				#endif
				return;
			}
	  	  #else
	    	sFile.open(fileName,O_WRITE );
	    	if(!sFile.isOpen()){
				#if defined (debug)
					Serial.println("fail");
				#endif
				return;
			}

	  	  #endif
		seek(44);
	 	}
		buffCount = 0; buffEmpty[0] = 1; buffEmpty[1] = 1;


		/*** This section taken from wiring_analog.c to translate between pins and channel numbers ***/
		#if defined(analogPinToChannel)
		#if defined(__AVR_ATmega32U4__)
			if (pin >= 18) pin -= 18; // allow for channel or pin numbers
		#endif
			pin = analogPinToChannel(pin);
		#elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
			if (pin >= 54) pin -= 54; // allow for channel or pin numbers
		#elif defined(__AVR_ATmega32U4__)
			if (pin >= 18) pin -= 18; // allow for channel or pin numbers
		#elif defined(__AVR_ATmega1284__) || defined(__AVR_ATmega1284P__) || defined(__AVR_ATmega644__) || defined(__AVR_ATmega644A__) || defined(__AVR_ATmega644P__) || defined(__AVR_ATmega644PA__)
			if (pin >= 24) pin -= 24; // allow for channel or pin numbers
		#else
			if (pin >= 14) pin -= 14; // allow for channel or pin numbers
		#endif

		#if defined(ADCSRB) && defined(MUX5)
			// the MUX5 bit of ADCSRB selects whether we're reading from channels
			// 0 to 7 (MUX5 low) or 8 to 15 (MUX5 high).
			ADCSRB = (ADCSRB & ~(1 << MUX5)) | (((pin >> 3) & 0x01) << MUX5);
		#endif

		#if defined(ADMUX)
			ADMUX = (pin & 0x07);
		#endif

		//Set up the timer
		if(recording > 1){

			TCCR1A = _BV(COM1A1); //Enable the timer port/pin as output for passthrough

		}
	    ICR1 = 10 * (1600000/SAMPLE_RATE);//Timer will count up to this value from 0;
		TCCR1A |= _BV(WGM11); //WGM11,12,13 all set to 1 = fast PWM/w ICR TOP
		TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10); //CS10 = no prescaling

		if(recording < 3){ //Normal Recording
			TIMSK1 |=  _BV(OCIE1B)| _BV(OCIE1A); //Enable the TIMER1 COMPA and COMPB interrupts
		}else{
			TIMSK1 |=  _BV(OCIE1B); //Direct pass through to speaker, COMPB only
		}


		ADMUX |= _BV(REFS0) | _BV(ADLAR);// Analog 5v reference, left-shift result so only high byte needs to be read
		ADCSRB |= _BV(ADTS0) | _BV(ADTS2);  //Attach ADC start to TIMER1 Compare Match B flag
		byte prescaleByte = 0;

		if(      SAMPLE_RATE < 18000){ prescaleByte = B00000110;} //ADC division factor 64 (16MHz / 64 / 13clock cycles = 19230 Hz Max Sample Rate )
		else if( SAMPLE_RATE < 27000){ prescaleByte = B00000101;} //32  (38461Hz Max)
		else{	                       prescaleByte = B00000100;} //16  (76923Hz Max)
		ADCSRA = prescaleByte; //Adjust sampling rate of ADC depending on sample rate
		ADCSRA |= _BV(ADEN) | _BV(ADATE); //ADC Enable, Auto-trigger enable


	}

	void TMRpcm::stopRecording(char *fileName){

		TIMSK1 &= ~(_BV(OCIE1B) | _BV(OCIE1A));
		ADCSRA = 0;
	    ADCSRB = 0;

		if(recording == 1 || recording == 2){
			recording = 0;
			unsigned long position = fPosition();
			#if defined (SDFAT)
				sFile.truncate(position);
			#endif
			sFile.close();
			finalizeWavTemplate(fileName);
		}
	}
#endif // not defined ENABLE_RECORDING

#endif // Not defined RF_ONLY

