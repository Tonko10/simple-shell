#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <termios.h>

// Globalne varijable za cuvanje stanja prije pokretanja nase ljuske
struct sigaction prije;
pid_t fog_pid = -1; //pamti koji proces se vrti ispred nas znc foreground

typedef struct {
    pid_t pid;
    char ime[256];
    int aktivan; // 1 ako radi 0 inace
} Proces;

Proces procesi[100]; // max 100 procesa u pozadini
int broj_procesa = 0; // gdje smo sad u polju

char povijest[1000][256]; //matrica za history
int broj_povijesti = 0; //kolko ih ima u povijesti do sad

// Handler za Ctrl+C
void obradi_dogadjaj(int sig)
{
    // Proslijedi signal procesu u prvom planu, a ljusku ostavi na zivotu
    if (fog_pid != -1) { //ak zapravo nesto radi ispred
        kill(fog_pid, SIGINT); //ubij samo taj proces
    }
    printf("\n");
}

// Handler kad dijete umre u pozadini, SIGCHLD
void obradi_signal_zavrsio_neki_proces_dijete(int id)
{

    pid_t pid_zavrsio = waitpid(-1, NULL, WNOHANG);
    if (pid_zavrsio > 0) { //ak je netko zavrsio radit
        if (kill(pid_zavrsio, 0) == -1) {
            // Oznaci ga mrtvim u nasoj tablici pozadinskih procesa
            for (int i = 0; i < broj_procesa; i++) {
                if (procesi[i].pid == pid_zavrsio) {
                    procesi[i].aktivan = 0;
                }
            }
            printf("Proces %d je zavrsio.\n", pid_zavrsio);
        }
    }
}

// Stvaranje procesa
pid_t pokreni_program(char *naredba[], int u_pozadini) //fja za vanjske programe
{
    pid_t pid_novi;

    if ((pid_novi = fork()) == 0) { // Mi smo u djetetu (fork vraca 0 djetetu a roditelju id)
        sigaction(SIGINT, &prije, NULL);
        setpgid(0, 0); // Stvori novu grupu za ovaj proces (pid=0 - za sebe)
        
        if (!u_pozadini) {
            // Ako nije u pozadini, dijete preuzima tipkovnicu i ekran
            tcsetpgrp(STDIN_FILENO, getpgid(0)); //daj djetetu kontrolu terminala
        }

        execvp(naredba[0], naredba);
        perror("Nisam pokrenuo program!"); //samo ak execvp otkaze
        exit(1);
    }

    return pid_novi; // Roditelj (ljuska) samo izadje iz funkcije i vrati taj broj
}

int main()
{
    struct sigaction act; //za postavljanje nasih handlera
    pid_t pid_novi;

    // SIGINT, SIGCHLD i SIGTTOU 
    act.sa_handler = obradi_dogadjaj; // handler za ctrlc
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART; // Sprjecava fgets da prekine kad Ctrl+C
    sigaction(SIGINT, &act, &prije);
    
    act.sa_handler = obradi_signal_zavrsio_neki_proces_dijete; //handler za kraj djeteta u pozadini
    sigaction(SIGCHLD, &act, NULL);
    
    act.sa_handler = SIG_IGN; // Ignoriramo SIGTTOU da mozemo prebacivati terminal bez da nas pauzira
    sigaction(SIGTTOU, &act, NULL);

    // Spremanje postavki terminala na pocetku
    struct termios shell_term_settings; //struktura za termios da vratimo na kraju
    tcgetattr(STDIN_FILENO, &shell_term_settings); //procitaj i spremi trenutne postavke

    // Uzmi natrag kontrolu nad terminalom
    tcsetpgrp(STDIN_FILENO, getpgid(0));
    
    size_t vel_buf = 128;
    char buffer[128]; // spremamo sta korisnik utipka
    char cwd[64]; // putanja foldera

    do { // Glavna petlja
        getcwd(cwd, sizeof(cwd)); // current working directory
        printf("%s$ ", cwd);

        if (fgets(buffer, vel_buf, stdin) != NULL) {
            
            // Makni ENTER na kraju
            buffer[strcspn(buffer, "\n")] = 0;
            if (strlen(buffer) == 0) 
                continue; // Prazan unos

            // Naredba !broj iz povijesti
            if (buffer[0] == '!') {
                int redni_br = atoi(&buffer[1]); //pretvori samo slova iza ! u int
                
                if (redni_br > 0 && redni_br <= broj_povijesti) {
                    strcpy(buffer, povijest[redni_br - 1]);
                    printf("%s\n", buffer);
                } else {
                    printf("Nepostojeca naredba u povijesti.\n");
                    continue;
                }
            }

            // Spremanje u povijest
            strcpy(povijest[broj_povijesti++], buffer); //spremi na zadnje i odma povecaj brojac

            // Parsiranje
            #define MAXARGS 5 //max rjeci
            char *argv[MAXARGS]; //niz pokazivaca na rjeci
            int argc = 0;
            argv[argc] = strtok(buffer, " \t\n"); //nadji prvu rjec

            while (argv[argc] != NULL) {
                argc++;
                argv[argc] = strtok(NULL, " \t\n");
            }

            // Provjera hoce li u pozadinu
            int pozadina = 0;
            if (strcmp(argv[argc - 1], "&") == 0) {
                pozadina = 1; //upali zastavicu
                argv[argc - 1] = NULL;
                argc--;
            }

            // naredbe
            if (strcmp(argv[0], "exit") == 0) { //exit
                // Sigurno gasenje svih pozadina
                for (int i = 0; i < broj_procesa; i++) {
                    if (procesi[i].aktivan) 
                        kill(procesi[i].pid, SIGKILL);
                }
                break;
            } else if (strcmp(argv[0], "cd") == 0) { //cd
                if (argc > 1) 
                    chdir(argv[1]);
                continue;
            } else if (strcmp(argv[0], "history") == 0) { //history
                for (int i = 0; i < broj_povijesti; i++) {
                    printf("  %d  %s\n", i + 1, povijest[i]);
                }
                continue;
            } else if (strcmp(argv[0], "ps") == 0) { //ps
                printf("PID\time\n");
                for (int i = 0; i < broj_procesa; i++) {
                    if (procesi[i].aktivan) {
                        printf("%d\t%s\n", procesi[i].pid, procesi[i].ime);
                    }
                }
                continue;
            } else if (strcmp(argv[0], "kill") == 0) { //kill
                if (argc >= 3) { //naredbu, id i signal
                    pid_t p = atoi(argv[1]);
                    int s = atoi(argv[2]);
                    int nas = 0;

                    // Osiguranje da ne ubijamo tudje procese
                    for (int i = 0; i < broj_procesa; i++) {
                        if (procesi[i].pid == p && procesi[i].aktivan) {
                            nas = 1; break;
                        }
                    }

                    if (nas) 
                        kill(p, s); //nas
                    else 
                        printf("Greska: To nije nas proces.\n");
                }
                continue;
            }

            // ostale naredbe
            pid_novi = pokreni_program(argv, pozadina); //nije rjec ugradjena - fork gore u f-ji

            if (!pozadina) {
                // Ako je u prvom planu (foreground)
                fog_pid = pid_novi; //zapisi njega kao prvog
                pid_t pid_zavrsio;
                
                // Cekamo dijete
                do {
                    pid_zavrsio = waitpid(pid_novi, NULL, 0);
                    if (pid_zavrsio > 0) { //nes smo uhvatili da zavrsava
                        if (kill(pid_novi, 0) == -1) {
                            // Vracamo kontrolu nad tipkovnicom ljusci
                            tcsetpgrp(STDIN_FILENO, getpgid(0));
                            tcsetattr(STDIN_FILENO, 0, &shell_term_settings); //stare postavke
                        } else { //nije mrtav
                            pid_novi = (pid_t) 0; // Jos radi
                        }
                    } else { // greska u waitpid
                        break;
                    }
                } while(pid_zavrsio <= 0);
                
                fog_pid = -1; // Nitko vise nije u prvom planu svedeno na nulu
            } else {
                // Pozadina - samo zapisemo podatke i nastavljamo
                procesi[broj_procesa].pid = pid_novi;
                strcpy(procesi[broj_procesa].ime, argv[0]);
                procesi[broj_procesa].aktivan = 1;
                broj_procesa++;
            }
        }
    } while(strncmp(buffer, "exit", 4) != 0);

    return 0;
}