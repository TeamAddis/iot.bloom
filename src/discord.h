#include "Arduino.h"
#include <ArduinoHttpClient.h>
#include <WiFiNINA.h>
#include "network_secrets.h"

const char discord_url[] = "discordapp.com";
const String discordWebhook = SECRET_DISCORD_WEBHOOK;
WiFiSSLClient sslClient;
HttpClient http_client = HttpClient(sslClient, discord_url, 443);

void sendMessageToDiscord(const String& message) {
    if (message == NULL) {return;}

    int messageLength = message.length();
    if (messageLength > 0) {

        Serial.println("[HTTP] Connecting to Discord...");
        Serial.println("[HTTP] Message: " + message);
        
        http_client.setHttpResponseTimeout(30000);
        http_client.post(discordWebhook, "application/json", "{\"content\":\"" + message + "\"}");

        // read the status code and body of the response
        int statusCode = http_client.responseStatusCode();
        String response = http_client.responseBody();

        Serial.print("[HTTP] Status code: ");
        Serial.println(statusCode);
        Serial.print("[HTTP] Response: ");
        Serial.println(response);

        http_client.stop();
        Serial.println("Disconnecting client from Discord.");
    } else {return;} 
}