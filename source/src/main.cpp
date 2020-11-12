//***************************************************************************************************
//                                      I T A G T E S T                                             *
//***************************************************************************************************
//*  Test for BLE Itag.                                                                             *
//*  React on the pushbutton of a cheap Bluetooth Itags.                                            *
//*  By Ed Smallenburg.                                                                             *
//***************************************************************************************************
// Note: If you try to connect to an Itag that is not switched on or absent, the 2nd call to the    *
// connect function will never return.  That is why we now connect to advertized Itags only.        *
// Hopefully a connect after disconnect will work.....                                              *
//***************************************************************************************************
// Configuration:                                                                                   *
// Enter the mac addresses of your Itags in the "Itag" table below.                                 *                                                                                                 *
//***************************************************************************************************
// Revision    Auth.  Remarks                                                                       *
// ----------  -----  ----------------------------------------------------------------------------- *
// 08-11-2020  ES     First set-up.                                                                 *
// 09-11-2020  ES     Only connect to advertized Itags.                                             *
//***************************************************************************************************
#include <Arduino.h>
#include <BLEDevice.h>

BLEUUID     batteryServiceUUID ( BLEUUID ( (uint16_t)0x180f ) ) ;    // The remote service we wish to connect to.
BLEUUID     batteryCharUUID    ( BLEUUID ( (uint16_t)0x2a19 ) ) ;    // The characteristic of the remote service we are interested in.
BLEUUID     buttonServiceUUID  ( BLEUUID ( (uint16_t)0xFFe0 ) ) ;    // Button notification service
BLEUUID     buttonCharUUID     ( BLEUUID ( (uint16_t)0xFFe1 ) ) ;    // Button notification characfteristics

// Data structure for holding data for every Itag
struct itag_t
{
  const char*               ItagAddress ;                           // Bluetooth addresses of Itags
  BLEAddress*               pServerAddress ;                        // BLE addresses for Itags
  BLEClient*                pClient ;                               // Client for communication
  BLERemoteCharacteristic*  pRemChar ;                              // Characteristics for Itag
  bool                      advertized ;                            // Device has advertized
  uint32_t                  oldmillis ;                             // Timestamp of last notify seen
} ;

// Configure the known Itags here:
itag_t   Itag[] = {                                                 // Data for the Itags
                    { "ff:ff:11:11:a2:fa", NULL, NULL, NULL, false, 0 },
                    { "ff:ff:22:21:ab:11", NULL, NULL, NULL, false, 0 },
                    { "ff:ff:77:70:4b:f0", NULL, NULL, NULL, false, 0 }
                  } ;

const int NITAGS = sizeof(Itag) / sizeof(itag_t) ;                  // Number of known Itags
BLEScan*  pBLEScan ;                                                // Scanner for devices

//***************************************************************************************************
//                                     M Y A D C                                                    *
//***************************************************************************************************
// Will be called for every advertizing device in the surrounding.                                  *
// We will check if this is one of our Itags and set the advertized flag if so.                     *
//***************************************************************************************************
class myADC: public BLEAdvertisedDeviceCallbacks
{
  void onResult ( BLEAdvertisedDevice advDev )
  {
    const char*  p ;                                     // Pont to mac address of advertizing server
    int          i ;                                     // Index in Itag[]
    bool         missing = false ;                       // There are still missing Itags (or not)

    p = advDev.getAddress().toString().c_str() ;
    for ( i = 0 ; i < NITAGS ; i++ )                     // Search for known address
    {
      if ( ! Itag[i].advertized )                        // Still missing?
      {
        if ( strcmp ( Itag[i].ItagAddress, p ) == 0 )    // Yes, found now?
        {
          Itag[i].advertized = true ;
          Serial.printf ( "Found address: %s\n",  p ) ;  // Show fitting mac address
        }
        else
        {
          missing = true ;
        }
      }
    }
    if ( !missing )                                      // Still absent Itag(s)?
    {
      advDev.getScan()->stop() ;                         // No, stop scan
    }
  }
} ;


//***************************************************************************************************
//                             B L E C L I E N T C A L L B A C K S                                  *
//***************************************************************************************************
// Call-back functions for connect and disconnect to/from an Itag.                                  *
//***************************************************************************************************
class MyClientCallbacks: public BLEClientCallbacks
{
  void onConnect ( BLEClient *pClient )                                  // Itag has connected
  {
    const char *p = pClient->getPeerAddress().toString().c_str() ;
    Serial.printf ( "Connected to Itag server %s\n", p ) ;
  } ;

  void onDisconnect ( BLEClient *pClient )                               // Itag has disconnected
  {
    const char *p = pClient->getPeerAddress().toString().c_str() ;
    pClient->disconnect() ;                                              // Disconnect client as well
    Serial.printf ( "Disconnected from Itag %s\n", p ) ;
  } ;
} ;


//***************************************************************************************************
//                          N O T E F Y C A L L B A C K                                             *
//***************************************************************************************************
// Will be called on every button push.                                                             *
// There will be a search for the Itag that caused the event.                                       *
//***************************************************************************************************
static void notifyCallback ( BLERemoteCharacteristic* pBLERemChar,
                             uint8_t* pData,  size_t length, bool isNotify )
{
  uint32_t        newmillis ;                             // Timestamp of new notify
  int             i ;                                     // INdex in pRemChar[]

  newmillis = millis() ;                                  // Get timestamp
  for ( i = 0 ; i < NITAGS ; i++ )                        // Search for the Itag
  {
    if ( Itag[i].pRemChar == pBLERemChar )                // Found Itag?
    {
      if ( newmillis > ( Itag[i].oldmillis + 500 ) )      // Yes, debounce
      {
        Itag[i].oldmillis = newmillis ;                   // New push seen, remember timestamp
        Serial.printf ( "Notify callback from Itag %d\n",
                        i ) ;
      }
      break ;                                             // No need to search further
    }
  }
}


//***************************************************************************************************
//                             C O N N E C T T O S E R V E R                                        *
//***************************************************************************************************
// Try to connect to the Itag.                                                                      *
//***************************************************************************************************
void connectToServer ( int i )
{
  BLERemoteService*               pRemoteService ;
  BLERemoteCharacteristic*        pbattchar ;                     // Characteristics for battery
  uint8_t                         battperc ;                      // Battery percentage
  bool                            res ;                           // Result of connect

  res = Itag[i].pClient->connect ( *Itag[i].pServerAddress ) ;    // Try to connect
  if ( ( ! Itag[i].pClient->isConnected() ) ||                    // Success?
       ( ! res ) )
  {
    Serial.printf ( "No server connection to Itag %d!\n", i ) ;   // No, show failure
    return ;
  }
  // Now, try to read the battery percentage
  pRemoteService = Itag[i].pClient->getService ( batteryServiceUUID ) ; 
  if ( pRemoteService == nullptr )
  {
    Serial.println ( "Failed to find battery service" ) ;
  }
  else
  {
    pbattchar = pRemoteService->getCharacteristic ( batteryCharUUID ) ;
    if ( pbattchar )                                              // Found characteristic?
    {
      battperc = pbattchar->readUInt8() ;                         // Yes, ead the value
      Serial.printf ( "Battery is %d percent\n", battperc ) ;
    }
  }
  pRemoteService = Itag[i].pClient->getService ( buttonServiceUUID ) ;
  if ( pRemoteService )
  {
    Serial.println ( "Found button service" ) ;
    Itag[i].pRemChar = pRemoteService->getCharacteristic ( buttonCharUUID ) ;
    if ( Itag[i].pRemChar )                                     // Characteristic found?
    {
      Itag[i].pRemChar->registerForNotify ( notifyCallback ) ;  // Yes, set callback function
      Serial.println ( "Notification Callback set" ) ;
    }
  }
  else
  {
    Serial.println ( "Service for button not found" ) ;
  }
}


//***************************************************************************************************
//                                   S E T U P                                                      *
//***************************************************************************************************
// Arduino set-up code.                                                                             *
//***************************************************************************************************
void setup() 
{
  int i ;                                                              // Itag number in loop

  Serial.begin ( 115200 ) ;
  Serial.println() ;
  Serial.println ( "Start Itag test...." ) ;
  BLEDevice::init ( "ESP32_Itag" ) ;
  for ( i = 0 ; i < NITAGS ; i++ )
  {
    Itag[i].pServerAddress = new BLEAddress ( Itag[i].ItagAddress ) ; // Address of Itag
    Itag[i].pClient  = BLEDevice::createClient() ;                    // Address of client
    Itag[i].pClient->setClientCallbacks ( new MyClientCallbacks() ) ; // Callbacks from network
  }
  pBLEScan = BLEDevice::getScan() ;                                   // Create scanner
  pBLEScan->setAdvertisedDeviceCallbacks ( new myADC() ) ;            // with callback
  pBLEScan->setActiveScan ( true ) ;                                  // For faster scan
} 


//***************************************************************************************************
//                                        L O O P                                                   *
//***************************************************************************************************
// Main loop of the program.                                                                        *
//***************************************************************************************************
void loop()
{
  int             i ;
  static uint32_t newcontime = 0 ;                         // Timestamp for new connect trial
  bool            scanrequired = false ;                   // Scan required or not
  
  if ( millis() > newcontime )                             // Time to try new connection?
  {
    for ( i = 0 ; i < NITAGS ; i++ )                       // Yes, set inactive Itags to not advertized
    {
      if ( !Itag[i].pClient->isConnected() )               // Is client connected?
      {
        Itag[i].advertized = false ;                       // No, set Itag to not advertized
        scanrequired = true ;                              // SCan is required
      }
    }
    if ( scanrequired )
    {
      Serial.println ( "Start scan" ) ;
      pBLEScan->start ( 10 ) ;                             // Scan for 10 seconds
    }
    for ( i = 0 ; i < NITAGS ; i++ )                       // Try all known Itags
    {
      if ( Itag[i].advertized )                            // Only for advertized Itags
      {
        if ( !Itag[i].pClient->isConnected() )             // Is client connected?
        {
          Serial.printf ( "Try connect to iTag %d, "       // No, show Itag info 
                          "mac is %s\n",
                          i, Itag[i].ItagAddress ) ;
          connectToServer ( i ) ;                          // Try to connect
        }
      }
    }
    newcontime = millis() + 5000 ;                         // Scedule new trial
  }
}
