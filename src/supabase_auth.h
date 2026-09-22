#pragma once

// Logs in to Supabase (GoTrue password grant) with the provisioned
// email/password and stores the access token + its expiry deadline.
// Skipped entirely if WiFi/internet isn't confirmed up.
void net_login();

// Called only while login_status is true. Once the token's deadline
// (millis()) passes, marks us logged-out and immediately retries net_login().
void checkTokenRefresh();
