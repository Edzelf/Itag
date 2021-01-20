//***************************************************************************************************
//                                      I T A G F I N D E R                                         *
//***************************************************************************************************
//*  Test for BLE iTag.                                                                             *
//*  React on cheap Bluetooth Itags.                                                                *
//*  By Ed Smallenburg.                                                                             *
//***************************************************************************************************
// This sketch searches for iTags.  The build-in LED will be on if at least 1 iTag is connected.    *
// If you have a buzzer connected, it will beep for every new connection to an iTag.                *
// The buzzer will also react on the push of the iTag button.                                       *
// Bug: trying to register notifications on button press did not work.  Thereore I made a patch to  *
// the library software.  For unpatched library set the definition of NOTIFY to 0.                  *
// The problem is that the iTags are not fully BLE compliant.                                       *
//***************************************************************************************************
// Configuration:                                                                                   *
// Enter the mac addresses of your Itags in the "Itag" table below.                                 *                                                                                                 *
//***************************************************************************************************
// Revision    Auth.  Remarks                                                                       *
// ----------  -----  ----------------------------------------------------------------------------- *
// 08-11-2020  ES     First set-up.                                                                 *
// 09-11-2020  ES     Only connect to advertized Itags.                                             *
// 11-01-2021  ES     Removed notify (causes crash).                                                *
// 19-01-2021  ES     Use library NimBLE.                                                           *
// 20-01-2012  ES     Start new scan after any connect or disconnect.                               *
//***************************************************************************************************
#include <Arduino.h>
#include <NimBLEDevice.h>                               // https://github.com/h2zero/NimBLE-Arduino

#define NOTIFY 1                                                        // Notify on button push (1=on, 0=off)
#define ACTLED LED_BUILTIN                                              // Flash on detect
#define BUZZER 25                                                       // Pin for buzzer output

NimBLEUUID  batteryServiceUUID ( NimBLEUUID ( (uint16_t)0x180f ) ) ;    // The remote service we wish to connect to.
NimBLEUUID  batteryCharUUID    ( NimBLEUUID ( (uint16_t)0x2a19 ) ) ;    // The characteristic of the remote service we are interested in.
NimBLEUUID  buttonServiceUUID  ( NimBLEUUID ( (uint16_t)0xFFe0 ) ) ;    // Button notification service
NimBLEUUID  buttonCharUUID     ( NimBLEUUID ( (uint16_t)0xFFe1 ) ) ;    // Button notification characfteristics

// Data structure for holding data for every Itag
struct itag_t
{
  const char*                 ItagAddress ;                             // Bluetooth addresses of Itags
  NimBLEAddress*              pServerAddress ;                          // BLE addresses for Itags
  NimBLEClient*               pClient ;                                 // Client for communication
  NimBLERemoteCharacteristic* pRemChar ;                                // Characteristics for Itag
  bool                        advertized ;                              // Device has advertized
  uint32_t                    oldmillis ;                               // Timestamp of last notify seen
} ;

// Configure the known Itags here:
itag_t   Itag[] = {                                                     // Data for the Itags
                    { "ff:ff:11:11:a2:fa", NULL, NULL, NULL, false, 0 },
                    { "ff:ff:22:21:ab:11", NULL, NULL, NULL, false, 0 },
                    { "ff:ff:77:70:4b:f0", NULL, NULL, NULL, false, 0 }
                  } ;

const int    NITAGS = sizeof(Itag) / sizeof(itag_t) ;                   // Number of known Itags
NimBLEScan*  pBLEScan ;                                                 // Scanner for devices
bool         restartscan = false ;                                      // Request to restart scan
int          beeptimer ;                                                // Timer for buzzer in msec

//**************************************************************************************************
//                                     B E E P                                                     *
//**************************************************************************************************
// Activate buzzer for a number of msec.                                                           *
//**************************************************************************************************
void beep ( int duration )
{
  beeptimer = duration ;                                 // Set timer for end of noise
  ledcWrite ( 0, 10 ) ;                                  // Start 
}


//**************************************************************************************************
//                                 H A N D L E _ B E E P T I M E R                                 *
//**************************************************************************************************
// Handle timing of the buzzer.                                                                    *
// Should be callled every 100 msec from main loop.                                                *
//**************************************************************************************************
void handle_beeptimer()
{
  if ( beeptimer > 0 )                            // Buzzer on?
  {
    beeptimer -= 100 ;                            // Yes, count duration
    if ( beeptimer <= 0 )                         // End reached?
    {
      ledcWrite ( 0, 0 ) ;                        // Yes, stop the noise
    }
  }
}


//***************************************************************************************************
//                                     M Y A D C                                                    *
//***************************************************************************************************
// Will be called for every advertizing device in the surrounding.                                  *
// We will check if this is one of our Itags and set the advertized flag if so.                     *
//***************************************************************************************************
class myADC: public NimBLEAdvertisedDeviceCallbacks
{
  void onResult ( NimBLEAdvertisedDevice* advDev )
  {
    const char*  p ;                                     // Pont to mac address of advertizing server
    int          i ;                                     // Index in Itag[]

    p = advDev->getAddress().toString().c_str() ;
    for ( i = 0 ; i < NITAGS ; i++ )                     // Search for known address
    {
      if ( ! Itag[i].advertized )                        // Still missing?
      {
        if ( strcmp ( Itag[i].ItagAddress, p ) == 0 )    // Yes, found now?
        {
          Itag[i].advertized = true ;
          Serial.printf ( "Found %s\n",                  // Yes, show info
                          advDev->toString().c_str() ) ;
        }
      }
    }
  }
} ;


//***************************************************************************************************
//                                 S C A N E N D E D C B                                            *
//***************************************************************************************************
// Call-back function.  Called when scan has ended.                                                 *
//***************************************************************************************************
void scanEndedCB ( NimBLEScanResults results )
{
  Serial.println ( "Scan Ended" ) ;
}


//***************************************************************************************************
//                             B L E C L I E N T C A L L B A C K S                                  *
//***************************************************************************************************
// Call-back functions for connect and disconnect to/from an Itag.                                  *
// On disconnect, the iTag will beep.                                                               *
//***************************************************************************************************
class MyClientCallbacks: public NimBLEClientCallbacks
{
  void onConnect ( NimBLEClient *pClient )                               // Itag has connected
  {
    char macstr[24] ;                                                    // mac address

    strcpy ( macstr, pClient->getPeerAddress().toString().c_str() ) ;
    Serial.printf ( "Connected to Itag server %s\n", macstr ) ;
    restartscan = true ;                                                 // Restart scan
  } ;

  void onDisconnect ( NimBLEClient *pClient )                               // Itag has disconnected
  {
    char macstr[24] ;                                                    // mac address
    int  i ;                                                             // Index in iTag[]

    strcpy ( macstr, pClient->getPeerAddress().toString().c_str() ) ;
    Serial.printf ( "Disconnected from Itag %s\n", macstr ) ;
    for ( i = 0 ; i < NITAGS ; i++ )                                    // Search in Itag[]
    {
      if ( strcmp ( Itag[i].ItagAddress, macstr ) == 0 )                // Match?
      {
        Itag[i].advertized = false ;                                    // Yes, set to not advertized
        break ;                                                         // No need to continue
      }
    }
    restartscan = true ;                                                // Restart scan
  } ;
} ;


//***************************************************************************************************
//                          N O T E F Y C A L L B A C K                                             *
//***************************************************************************************************
// Will be called on every button push.                                                             *
// There will be a search for the Itag that caused the event.                                       *
//***************************************************************************************************
static void notifyCallback ( NimBLERemoteCharacteristic* pBLERemChar,
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
        beep ( 500 ) ;                                    // Blow the horn ( short beep )

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
  NimBLERemoteService*            pRemoteService ;
  NimBLERemoteCharacteristic*     pbattchar ;                     // Characteristics for battery
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
      battperc = pbattchar->readValue<uint8_t>() ;                         // Yes, read the value
      Serial.printf ( "Battery is %d percent\n", battperc ) ;
    }
  }
  pRemoteService = Itag[i].pClient->getService ( buttonServiceUUID ) ;
  if ( pRemoteService )
  {
    Serial.println ( "Found button service" ) ;
    if ( NOTIFY )
    {
      Itag[i].pRemChar = pRemoteService->getCharacteristic ( buttonCharUUID ) ;
      if ( Itag[i].pRemChar )                                              // Characteristic found?
      {
        if ( Itag[i].pRemChar->canNotify() )
        {
          Serial.println ( "Service can notify" ) ;
          // Subscribe tries to write the value of the first parameter to descriptor 0x2902.
          // It seams that there is no 0x2902 descriptor, so "false " is returned
          if ( Itag[i].pRemChar->getDescriptor ( NimBLEUUID ( (uint16_t)0x2902 ) ) )
          {
            Serial.println ( "Descriptor 0x2902 exists" ) ;
          }
          else
          {
            Serial.println ( "Descriptor 0x2902 does not exists, continue anyway" ) ;
          }
          if ( Itag[i].pRemChar->subscribe ( true, notifyCallback, false ) )   // Yes, set callback function
          {
            Serial.println ( "Notification Callback set" ) ;
          }
          else
          {
            Serial.println ( "Setting Notification Callback failed!" ) ;
          }
        }
      }
    }
    else
    {
      Serial.println ( "Button notify service not activated" ) ;
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
  Serial.println ( "Start Itag finder...." ) ;
  pinMode ( ACTLED, OUTPUT ) ;                                        // Prepare LED pin
  ledcSetup ( 0, 2000, 8 ) ;                                          // Buzzer, channel 0, 2 kHz, 8 bits
  ledcAttachPin ( BUZZER, 0 ) ;                                       // Connect GPIO to channel 0
  NimBLEDevice::init ( "ESP32_Itag_finder" ) ;
  for ( i = 0 ; i < NITAGS ; i++ )
  {
    Itag[i].pServerAddress = new NimBLEAddress ( Itag[i].ItagAddress ) ; // Address of Itag
    Itag[i].pClient = BLEDevice::createClient() ;                     // Address of client
    Itag[i].pClient->setClientCallbacks ( new MyClientCallbacks() ) ; // Callbacks from network
  }
  pBLEScan = NimBLEDevice::getScan() ;                                // Create scanner
  pBLEScan->setAdvertisedDeviceCallbacks ( new myADC() ) ;            // With callback
  pBLEScan->setInterval ( 45 ) ;                                      // For 45 times
  pBLEScan->setWindow ( 15 ) ;                                        // Lengt of one scan in msec
  pBLEScan->setActiveScan ( true ) ;                                  // For faster scan
  pBLEScan->start ( 0, scanEndedCB ) ;                                // Scan forever, dummy call-back
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
  
  if ( millis() > newcontime )                             // Time to try new connection?
  {
    digitalWrite ( ACTLED, LOW ) ;                         // Activity LED off
    for ( i = 0 ; i < NITAGS ; i++ )                       // Try all known Itags
    {
      if ( Itag[i].advertized )                            // Only for advertized Itags
      {
        digitalWrite ( ACTLED, HIGH ) ;                    // LED on if at least one connected iTag
        if ( !Itag[i].pClient->isConnected() )             // Is client connected?
        {
          Serial.printf ( "Try connect to iTag %d, "       // No, show Itag info 
                          "mac is %s\n",
                          i, Itag[i].ItagAddress ) ;
          connectToServer ( i ) ;                          // Try to connect
          beep ( 1000 ) ;                                  // Blow the horn ( long beep )
        }
      }
    }
    newcontime = millis() + 5000 ;                         // Schedule new trial
  }
  if ( restartscan )                                       // Request to restart scan?
  {
    restartscan = false ;                                  // Yes, reset request
    pBLEScan->stop() ;                                     // Stop scan
    pBLEScan->clearResults() ;                             // Delete results
    pBLEScan->start ( 0, scanEndedCB ) ;                   // Scan again
  }
  handle_beeptimer() ;                                     // Handle timing of buzzer
  delay ( 100 ) ;
}
