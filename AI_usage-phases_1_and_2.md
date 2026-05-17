# AI Usage Documentation — Phases 1 & 2
## Project: City Infrastructure Issue Reporting System

---

## Tool Used

**Claude (Anthropic) — claude.ai**

---

# Phase 1

## 1. `parse_condition`

### Prompt dat AI-ului

"Am o structură C numită Report cu câmpurile: id (int), inspector (char[50]), lat/lon (float), category (char[30]), severity (int), timestamp (time_t), description (char[256]).
Am nevoie de o funcție: int parse_condition(const char *input, char *field, char *op, char *value); care să împartă un șir de caractere de tip condiție, de forma camp:operator:valoare, în cele trei părți componente.
Operatorii suportați sunt ==, !=, <, <=, >, >=.
Returnează 1 în caz de succes, 0 în caz de eșec."

### Ce a generat AI-ul

```c
int parse_condition(const char *input, char *field, char *op, char *value) {
    char tmp[256];
    strncpy(tmp, input, 255);
    char *tok = strtok(tmp, ":");
    if (!tok) return 0;
    strcpy(field, tok);
    tok = strtok(NULL, ":");
    if (!tok) return 0;
    strcpy(op, tok);
    tok = strtok(NULL, ":");
    if (!tok) return 0;
    strcpy(value, tok);
    return 1;
}
```

### Probleme identificate la revizuire

1. `strtok` modifică șirul direct (in-place). Deși am folosit o copie (`tmp`), dacă value ar conține la rândul său două puncte (ex: un timestamp în format ISO), `strtok` l-ar fragmenta incorect — al treilea `strtok(NULL, ":")` s-ar opri la primul simbol `:` din interiorul valorii.
2. Lipsa verificării limitelor bufferelor destinație — potențială depășire de buffer (overflow) dacă input-ul este foarte lung.
3. Câmpul `op` a fost citit cu același delimitator `":"`. Deși tehnic funcționează, funcția nu poate distinge între un operator lipsă și o valoare lipsă.

### Ce am modificat

Am înlocuit abordarea cu `strtok` folosind `sscanf` cu format de excludere a clasei de caractere: `%49[^:]:%9[^:]:%99s`. Acesta citește până la simbolul `:` pentru `field` și `op`, apoi citește restul șirului pentru `value`. Am redus și lățimile de format pentru a corespunde dimensiunilor reale ale bufferelor.

```c
int parse_condition(const char *input, char *field, char *op, char *value) {
    int r = sscanf(input, "%49[^:]:%9[^:]:%99s", field, op, value);
    return r == 3;
}
```

---

## 2. `match_condition`

### Prompt dat AI-ului

"Folosind aceeași structură Report, implementează:
int match_condition(const Report *r, const char *field, const char *op, const char *value);
Ar trebui să returneze 1 dacă raportul îndeplinește condiția, 0 în caz contrar.
Câmpuri suportate: severity, category, inspector, timestamp.
Operatori suportați: ==, !=, <, <=, >, >=."

### Ce a generat AI-ul

```c
int match_condition(const Report *r, const char *field, const char *op, const char *value) {
    if (strcmp(field, "severity") == 0) {
        int v = atoi(value);
        if (strcmp(op, "==") == 0) return r->severity == v;
        if (strcmp(op, ">=") == 0) return r->severity >= v;
        if (strcmp(op, "<=") == 0) return r->severity <= v;
        if (strcmp(op, ">")  == 0) return r->severity >  v;
        if (strcmp(op, "<")  == 0) return r->severity <  v;
        if (strcmp(op, "!=") == 0) return r->severity != v;
    }
    if (strcmp(field, "category") == 0) {
        if (strcmp(op, "==") == 0) return strcmp(r->category, value) == 0;
        if (strcmp(op, "!=") == 0) return strcmp(r->category, value) != 0;
    }
    if (strcmp(field, "inspector") == 0) {
        if (strcmp(op, "==") == 0) return strcmp(r->inspector, value) == 0;
    }
    return 0;
}
```

### Probleme identificate la revizuire

1. Câmpul `timestamp` lipsea complet, deși l-am menționat explicit în prompt. A trebuit să îl adaug manual.
2. Câmpului `inspector` îi lipsea operatorul `!=`.
3. Funcția returnează 0 fără nicio notificare pentru câmpuri/operatori necunoscuți — am adăugat un diagnostic `fprintf(stderr, ...)` pentru a facilita depanarea.
4. `atoi` pentru `timestamp` este greșit pentru valori mari de tip Unix timestamp (risc de overflow pe 32 de biți). Am schimbat în `atol` cu cast la `time_t`.

### Ce am modificat

Am adăugat blocul pentru `timestamp` folosind `atol`, am adăugat `!=` pentru `inspector` și un mesaj de eroare pentru input-uri necunoscute. Versiunea completă corectată se află în `city_manager.c`.

### Ce am învățat

- Asistenții AI sunt buni la generarea unui schelet pentru logica repetitivă, dar pot omite câmpuri sau operatori specificați în prompt. Fiecare linie de cod generat trebuie verificată față de cerințele reale.
- `strtok` este o alegere comună a AI-ului pentru fragmentarea șirurilor, dar este nesigură când delimitatorii apar în interiorul valorilor. `sscanf` cu clase de caractere este mai curat și mai sigur.
- Distincția dintre `atoi` și `atol` contează pentru valorile `time_t` pe sisteme pe 64 de biți. AI-ul nu a gestionat acest aspect.
- Scrierea unui prompt precis nu a împiedicat AI-ul să omită o parte din cerințe. Revizuirea conform specificațiilor este întotdeauna necesară.

---

# Phase 2

## 3. Structura de semnale pentru `monitor_reports`

### Prompt dat AI-ului

"Scrie un program C numit monitor_reports care folosește sigaction() (nu signal()) pentru SIGINT și SIGUSR1. La SIGUSR1 să afișeze un mesaj că a fost adăugat un raport. La SIGINT să afișeze un mesaj de închidere, să șteargă .monitor_pid și să iasă. Să folosească pause() în bucla principală."

### Ce a generat AI-ul

AI-ul a generat structura de bază cu handlere și `pause()`.

### Probleme identificate la revizuire

1. **Handlerul apela `printf` direct** — `printf` nu este async-signal-safe. Am mutat printul în main loop: handlerul doar setează un flag `int`, iar `printf` este apelat după ce `pause()` returnează.
2. **`SA_RESTART` era setat** pe `sa_flags` — cu `SA_RESTART`, `pause()` ar fi restartat automat după semnal și nu ar fi returnat niciodată, deci monitorul nu ar fi afișat nimic. Am eliminat flagul.
3. **PID scris cu `fprintf`** — am schimbat în `open/write/close` conform cerinței de a folosi system calls.

## 4. Funcția `notify_monitor` din `city_manager`

### Prompt dat AI-ului

"Cum ar trebui city_manager să citească un PID dintr-un fișier și să trimită SIGUSR1, gestionând toate cazurile de eroare?"

### Ce a generat AI-ul

AI-ul a generat pattern-ul de bază open/read/atoi/kill.

### Probleme identificate la revizuire

1. **`atoi` folosit pentru PID** — am schimbat în `atol` cu cast la `pid_t` pentru portabilitate.
2. **Mesajul de log scris prin `log_action`** care sare inspectorii — cerința spune că rezultatul notificării trebuie să apară în log indiferent de rol. Am adăugat o funcție separată `log_monitor_result()` care scrie direct în fișier fără verificarea rolului.
