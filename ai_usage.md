Am o structură C numită Report cu câmpurile: id (int), inspector (char[50]), lat/lon (float), category (char[30]), severity (int), timestamp (time_t), description (char[256]).
Am nevoie de o funcție: int parse_condition(const char *input, char *field, char *op, char *value); care să împartă un șir de caractere de tip condiție, de forma camp:operator:valoare, în cele trei părți componente.
Operatorii suportați sunt ==, !=, <, <=, >, >=.
Returnează 1 în caz de succes, 0 în caz de eșec.”
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
Probleme identificate la revizuire:
strtok modifică șirul direct (in-place). Deși am folosit o copie (tmp), dacă value ar conține la rândul său două puncte (ex: un timestamp în format ISO), strtok l-ar fragmenta incorect — al treilea strtok(NULL, ":") s-ar opri la primul simbol : din interiorul valorii.
Lipsa verificării limitelor bufferelor destinație — potențială depășire de buffer (overflow) dacă input-ul este foarte lung.
Câmpul op a fost citit cu același delimitator ":". Deși tehnic funcționează, funcția nu poate distinge între un operator lipsă și o valoare lipsă.

Ce am modificat:

Am înlocuit abordarea cu strtok folosind sscanf cu format de excludere a clasei de caractere: %49[^:]:%9[^:]:%99s. Acesta citește până la simbolul : pentru field și op, apoi citește restul șirului pentru value. De asemenea, am redus lățimile de format pentru a corespunde dimensiunilor reale ale bufferelor din semnătura funcției.
int parse_condition(const char *input, char *field, char *op, char *value) {
    int r = sscanf(input, "%49[^:]:%9[^:]:%99s", field, op, value);
    return r == 3;
}

Prompt 2 — match_condition

„Folosind aceeași structură Report, implementează: int match_condition(const Report *r, const char *field, const char *op, const char *value);
Ar trebui să returneze 1 dacă raportul îndeplinește condiția, 0 în caz contrar.
Câmpuri suportate: severity, categ severity, category, inspector, timestamp.
Operatori suportați: ==, !=, <, <=, >, >=.”
int match_condition(const Report *r, const char *field, const char *op, const char *value) {
    if (strcmp(field, "severity") == 0) {
        int v = atoi(value);
        if (strcmp(op, "==") == 0)
            return r->severity == v;
        if (strcmp(op, ">=") == 0)
            return r->severity >= v;
        if (strcmp(op, "<=") == 0)
            return r->severity <= v;
        if (strcmp(op, ">") == 0)
            return r->severity >  v;
        if (strcmp(op, "<") == 0)
            return r->severity <  v;
        if (strcmp(op, "!=") == 0)
            return r->severity != v;

    }

    if (strcmp(field, "category") == 0) {
        if (strcmp(op, "==") == 0)
            return strcmp(r->category, value) == 0;
        if (strcmp(op, "!=") == 0)
            return strcmp(r->category, value) != 0;

    }

    if (strcmp(field, "inspector") == 0) {
        if (strcmp(op, "==") == 0)
        return strcmp(r->inspector, value) == 0;

    }
    return 0;

}

Probleme identificate la revizuire:

Câmpul timestamp lipsea complet, deși l-am menționat explicit în prompt. A trebuit să îl adaug manual.
Câmpului inspector îi lipsea operatorul !=.
Funcția returnează 0 fără nicio notificare pentru câmpuri/operatori necunoscuți — am adăugat un diagnostic fprintf(stderr, ...) pentru a facilita depanarea în timpul testării.
atoi pentru timestamp este greșit pentru valori mari de tip Unix timestamp (risc de overflow pe 32 de biți pe unele sisteme). Am schimbat în atol cu conversie (cast) la time_t.

Ce am modificat:

Am adăugat blocul pentru timestamp folosind atol, am adăugat != pentru inspector și un mesaj de eroare pentru input-uri necunoscute. Versiunea completă corectată se află în city_manager.c.

Ce am învățat:

Asistenții AI sunt buni la generarea unui schelet pentru logica repetitivă de comparare, dar pot omite „tăcut” câmpuri sau operatori specificați în prompt. Fiecare linie de cod generat trebuie verificată față de cerințele reale.
strtok este o alegere comună a AI-ului pentru fragmentarea șirurilor, dar este nesigură pe literale de șir și eșuează când delimitatorii apar în interiorul valorilor. sscanf cu modele de clase de caractere este o metodă mai curată și mai sigură.
Distincția între atoi și atol contează pentru valorile time_t pe sistemele pe 64 de biți. AI-ul nu a gestionat acest aspect corect.
Scrierea unui prompt precis (listarea explicită a tuturor câmpurilor și operatorilor) nu a împiedicat AI-ul să omită o parte din ele. Revizuirea conform specificațiilor este întotdeauna necesară.
Cea mai valoroasă parte a exercițiului nu este codul generat în sine, ci procesul de citire critică a acestuia și înțelegerea motivelor pentru care fiecare linie este corectă sau incorectă.