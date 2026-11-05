# Gmail SMTP and MacroDroid webhook setup

Connect SAFE ROAD to an internet-connected Wi-Fi network. Open its dashboard at
http://192.168.4.1 while connected to the device hotspot. Go to **Wi-Fi &
notifications** and select **Load saved settings**.

## Gmail SMTP

1. In the Google account that will send alerts, turn on **2-Step Verification**.
   [Google setup instructions](https://support.google.com/accounts/answer/185839)
2. Open **App passwords**, create one named `SAFE ROAD`, and copy the generated
   password. Some work or school accounts and accounts with Advanced Protection
   may restrict app passwords. [Create an app password](https://myaccount.google.com/apppasswords) · [Google app password help](https://support.google.com/accounts/answer/185833)
3. On the SAFE ROAD dashboard, enable **Gmail email** and fill in the fields below.

   | Dashboard field | What to enter |
   | --- | --- |
   | Gmail sender | The full Gmail address used to create the app password |
   | Send alerts to | The address that should receive alerts. You can use your own for the first test. |
   | Gmail app password | The generated 16-character app password. The dashboard strips spaces. |

4. Select **Save notifications**, then **Test email**. Check the delivery status
   in the dashboard and the recipient's inbox or Spam folder. If authentication
   fails, verify that the sender address matches the account that created the
   app password.

The firmware already uses `smtp.gmail.com` on port `465` with TLS. Enter the
app password only in the device dashboard. [SMTP connection code](../main/alerts.cpp)

## MacroDroid example

1. On your Android phone, create a macro named **SAFE ROAD Alert**.
2. Add **Webhook (URL)** as the trigger and enter `safe_road` as the identifier.
   Copy the full URL shown by MacroDroid. It should have this shape:

   ```text
   https://trigger.macrodroid.com/<device-id>/safe_road
   ```

3. Add the **Display Notification** action. Use `SAFE ROAD Alert` for the title
   and `A monitoring event was received.` for the message. Save and enable the
   macro. Allow MacroDroid notifications in Android if prompted.
4. On the SAFE ROAD dashboard, enable **Webhook**, paste the complete URL into
   **Webhook URL**, and select **Save notifications**.
5. Select **Test webhook**. Check the dashboard's delivery result and look for
   the notification on your phone. Both devices need internet access.

If the dashboard reports **HTTP 404**, compare the saved URL with the trigger
URL in MacroDroid. The path must include the exact identifier after the device
ID. Keep the complete trigger URL private. [MacroDroid webhook trigger guide](https://macrodroidforum.com/wiki/index.php/Trigger%3A_Webhook_%28URL%29)

For a custom macro, MacroDroid can save the POST body to a string variable.
SAFE ROAD sends JSON with fields including `event`, `device`, `duration_ms`,
`uptime_ms`, and `text`. The test uses the event name `test`. The numeric
duration and uptime are in milliseconds, and the human-readable message uses
seconds. [Webhook payload code](../main/alerts.cpp)
