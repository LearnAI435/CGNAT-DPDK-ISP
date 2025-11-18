# Email Domains Security Runbook

Purpose
- Provide operational procedures to prevent, detect, and respond to incidents involving the project's email domains (including spoofing, phishing, unauthorized DNS/registrar changes, certificate misuse, and mailbox compromise).

Scope
- All DNS records, registrar accounts, mail flows, TLS certificates, SPF/DKIM/DMARC configurations, and inboxes and service accounts that send or receive email on behalf of the project's domains.
- Maintainers, release automation, CI notifications, and any third-party services that send mail using the project domains.

Key Contacts
- Maintainer(s): @LearnAI435 (primary)
- Secondary contact: (add name/email)
- Security lead / incident manager: (add name/email)
- Registrar account owner and support contact: (add details)
- Hosting/DNS provider support: (add details)

Preventive Controls (Checklist)
- Registrar lock (transfer lock) enabled.
- Two-factor authentication (2FA) enabled for registrar, DNS provider, certificate manager, and mailbox accounts.
- DNS provider: RBAC for users; limit API keys to minimum privileges.
- DNS records: maintain canonical list in repo (infrastructure as code / DNS as code) and require PR review to change.
- SPF: Publish strict SPF record that enumerates authorized senders and uses -all where appropriate.
- DKIM: Rotate and store private keys securely (vault), publish corresponding public keys in DNS.
- DMARC: Enforce a DMARC policy (p=quarantine or p=reject) with aggregate (rua) and forensic (ruf) reporting to monitored mailboxes.
- MTA/TLS: Force STARTTLS and prefer TLS 1.2+; monitor certificate validity and automate renewals.
- IAM: Restrict service accounts that can send email; use dedicated service accounts per service.
- Secrets management: Keep email-related credentials out of repo; use secret store (e.g., HashiCorp Vault, GitHub Secrets).

Monitoring and Detection
- DMARC aggregate reports (rua) ingested into a DMARC analytics tool and reviewed weekly.
- SPF/DKIM/DMARC pass/fail alerts piped into project Slack/Teams channel.
- DNS change monitoring: alert on zone changes and on WHOIS changes.
- Certificate transparency (CT) monitoring: alert on new certificates issued for project domains.
- Suspicious inbound messages: route to a mailbox monitored by the security lead; set up automated sandboxing if possible.
- Email sending quotas and unusual volume detection for sending services.

Incident Classification (examples)
- Spoofing/Phishing: External actor forging emails from project domains (SPF/DKIM fail but spoofed From).
- DNS or Registrar Compromise: Unauthorized DNS record changes, transfer attempts, or WHOIS modification.
- Mailbox Compromise: Unauthorized access to a project mailbox or service account that can send signed mail.
- Certificate Mis-issuance: A TLS certificate issued for a project domain unexpectedly.

Immediate Response Playbooks
1) Spoofing / Phishing Detected
- Priority: High (reputation + user risk).
- Confirm scope: gather sample messages, headers, timestamps, and recipient list.
- Check SPF/DKIM/DMARC results for samples and confirm origin IP addresses.
- If only external spoofing (not sending from owned systems): increase DMARC policy to p=reject if monitoring shows legitimate flows covered; update RUA/RUF recipients; notify mail providers (Gmail/Yahoo) via abuse/contact forms if campaign is large.
- Notify maintainers and post a short advisory in project security channel.
- Publish guidance for users (how to verify legit messages, what not to click) if campaign targets users.
- Retain raw headers and samples for forensics.

2) DNS or Registrar Compromise
- Priority: Critical.
- Immediately change registrar account password and disable API keys; enable/confirm 2FA. If control is lost, contact registrar support and escalate with ownership proof (domain registration email, signed commit, org paperwork).
- Use DNS provider console to revert unauthorized changes or restore from backups/previous zone file. If zone is controlled elsewhere, coordinate with provider to restore and propagate.
- Revoke or reissue any impacted DKIM keys and TLS certs.
- Rotate secrets for mail-sending service accounts.
- Notify affected users and project stakeholders; consider DMARC quarantine/reject while recovering.
- Open an incident ticket and track timeline, actions, and communications.

3) Mailbox / Service Account Compromise
- Priority: High.
- Revoke sessions and reset passwords for the compromised account(s); revoke API tokens and rotate keys.
- Revoke or rotate DKIM keys associated with the compromised mailbox/service.
- Review recent sent messages and delete/recall where supported; send notifications to recipients if sensitive content was sent.
- Run endpoint scans for malware where the mailbox owner uses a local device.
- Increase monitoring for spam/abuse originating from project domains.

4) Certificate Mis-issuance
- Priority: High.
- Validate certificate (CT logs) to confirm issuance.
- If certificate is unauthorized, contact the CA to request revocation and request remediation.
- Notify CDNs or reverse proxies to block the unauthorized cert if possible.

Post-incident Actions
- Full incident review: timeline, root cause, steps taken, gaps found, and mitigation plan.
- Rotate impacted credentials and keys.
- Update preventive controls (e.g., add 2FA, tighten DMARC policy, change DNS provider).
- Communicate an incident report to stakeholders and publish a public advisory if user data or trust was affected.

Operational Playbooks and Procedures
- How to update SPF/DKIM/DMARC safely:
  - Make changes in infra-as-code (PR) and run DNS lint/validators locally.
  - Monitor DMARC reports for 72 hours after changes.
  - Phased rollout: start with p=none + reporting, analyze, then move to quarantine/reject.
- How to rotate DKIM keys:
  - Generate new key pair in vault; publish new public key in DNS under new selector; update MTA/service to sign with the new key; wait for propagation; retire old selector after 7 days (or as policy).
- How to renew/replace TLS certs:
  - Use ACME automation (Let's Encrypt) or ensure CA renewal is scheduled; inventory all endpoints serving project domains beforehand.
- How to decommission a sending service:
  - Remove from SPF include lists, remove DKIM selectors, and add to DMARC exempt list until fully decommissioned.

Evidence collection checklist
- Preserve raw email headers and full message bodies (in quarantine).
- Export DNS zone file snapshots and registrar WHOIS history.
- Backup relevant logs (mail server logs, API access logs, authentication logs) with timestamps.

Automation & Tooling Recommendations
- DMARC analysis: use a service or open-source parser to centralize RUA reports.
- DNS monitoring: DNS difference alerts + WHOIS change alerting.
- Certificate transparency monitoring service alerts.
- Integrate alerts into on-call channel (PagerDuty/Slack).
- Automate DKIM rotation and TLS renewal where possible.

Documentation & Runbook Maintenance
- Keep this runbook in the repo under .github/RUNBOOKS; require at least two reviewers for changes.
- Review quarterly or after any domain-related incident.
- Maintain an inventory file (.github/RUNBOOKS/email-domain-inventory.yml) listing: domains, registrar, DNS provider, DKIM selectors, contacts, and recovery procedures.

Appendix: Quick Commands & Checks
- Check SPF/DKIM/DMARC: use online tools (eg. DMARC analyzers) or run: dig TXT yourdomain.com, dig TXT selector._domainkey.yourdomain.com
- Check DNS changes: DNS zone diff or compare against stored zone file.
- Check CT logs: use crt.sh or certificate monitoring API.

