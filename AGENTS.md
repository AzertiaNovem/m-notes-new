# Second project: mistakebook-native

This is an independent C++20 / SQLite / React implementation. Keep the original project's code, secrets, and data untouched. The user explicitly authorized the Alpha backend at root@cpa.missazertia.com with https://m-notes-alpha.missazertia.com. It runs independently as mistakebook-alpha on 127.0.0.1:8080, with releases in /opt/mistakebook-alpha and data in /var/lib/mistakebook-alpha. Never replace the legacy /opt/mistakebook application, its database, or its port 8000 service. See docs/DEPLOYMENT-ALPHA.md and deploy/ for the verified deployment configuration. Build and test before updating the active release.

Backend: backend/. Frontend and shared rendering/types: packages/web and packages/shared. C++ API owns storage and all authorization. Every record must be scoped to the authenticated primary owner's id. Read-only subaccounts share that owner's read access and cannot mutate content or manage accounts. Maximum five subaccounts must be enforced in a SQLite transaction. Passwords are Argon2id hashes; session tokens are random, hashed in storage. Never export password hashes or tokens.

Use prepared SQL, WAL, foreign keys, bounded request sizes. Keep frontend visually close to the source project. Do not add a backend text-generation model. Keep source diagrams SVG-only.
