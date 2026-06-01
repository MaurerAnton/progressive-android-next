# Progressive Android Next — Screenshots

GPU framebuffer captures of all app screens (v0.5.5-pre).
Captured on Samsung SM-G770F (1080×2400, Android 13) via `glReadPixels` → PPM → PNG.

## Screens

| File | Screen | Description |
|------|--------|-------------|
| `server.png` | SCR_SERVER | Protocol selection + onboarding carousel |
| `matrix.png` | SCR_MATRIX | Matrix login form (HS URL, username, password) |
| `irc.png` | SCR_IRC | IRC connection form (server, nick, TLS toggle) |
| `signup.png` | SCR_SIGNUP | Create account form |
| `profile.png` | SCR_PROFILE | User profile view |
| `settings.png` | SCR_SETTINGS | App settings (notifications, etc.) |
| `roominfo.png` | SCR_ROOMINFO | Room info / management |
| `chatlist.png` | SCR_CHATLIST | Room list / chat overview |
| `chat.png` | SCR_CHAT | Active chat with messages |

## Capture Method

GPU framebuffer capture bypasses Samsung FreecessController which hides the app window (`viewVisibility=8`). The app renders to an offscreen GL surface; `glReadPixels` reads the actual rendered content.

Capture is triggered automatically on screen transitions via `captureNext` flag in the render loop.
