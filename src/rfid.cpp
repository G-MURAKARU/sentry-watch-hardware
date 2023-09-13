#include <Arduino.h>
#include "main.h"

/*
*	library to interact with RFID card reader, includes SPI.h
*/
#include <MFRC522.h>


/* active MFRC instance */
static MFRC522 reader(MFRC_SS_PIN, MFRC_RST_PIN);
MFRC522::MIFARE_Key key;

/* string variable to store stringified RFID UID */
String card_id;


/**
 * initialize_rfid - sets up the RFID MFRC module on the SPI bus
 *
 * Return: Nothing
 *
 * Note: SPI.begin() should be called prior to this
*/
void initialize_rfid()
{
    reader.PCD_Init();

	for (byte i = 0; i < 6; i++)
	{
		key.keyByte[i] = 0xFF;
	}
}

/**
 * dump_byte_array - dumps the scanned hex RFID UID into a string literal
 *
 * @buffer: buffer storing scanned RFID UID
 * @buffer_size: length of the RFID UID in bytes (4, 7 or 10)
 *
 * Return: string literal representation of the scanned RFID UID
*/
static void dump_byte_array(byte *buffer, byte buffer_size)
{
	/* reset stored RFID UID to an empty string for new scan */
	card_id = "";

	for (byte i = 0; i < buffer_size; i++)
	{
		if (i == 0)
			card_id += (buffer[i] < 0x10 ? "0" : "");
		else
			card_id += (buffer[i] < 0x10 ? " 0" : " ");

		card_id += String(buffer[i], HEX);
	}
}

/**
 * rfid_read_new_card - checks if a new card is available to be read and
 *  reads it if there is saving its UID to card_id
 *
 * Return: true if a new card is available and has been successfully
 *  read, false otherwise
*/
bool rfid_read_new_card()
{
	/* checking if there is a 'new' RFID card in vicinity to scan */
	if (!reader.PICC_IsNewCardPresent())
		return false;

	if (!reader.PICC_ReadCardSerial())
		return false;

	/* dumping the scanned card's ID (hex number) into a string */
	dump_byte_array(reader.uid.uidByte, reader.uid.size);

	reader.PICC_HaltA();
  	reader.PCD_StopCrypto1();

    return true;
}
