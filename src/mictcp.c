#include <mictcp.h>
#include <../include/api/mictcp_core.h> 
#include <pthread.h>

//TODO : remplacer par des perror + ajouter des return -1

#define TAUX_PERTES 5
#define TIMER 10
#define SIZE_WINDOW 100 // taille de la fenetre glissante
#define MAX_ECHECS 10


// Variables globales :
mic_tcp_sock sock = {0};
mic_tcp_sock_addr addr_socket_dest = {0};
int PE = 0; // prochaine trame à émettre
int PA = 0; // prochaine trame attendue
int numero_paquet = 0;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/* creation de la fenetre glissante
le tableau sera initialisé avec que des 0 */
int fenetre[SIZE_WINDOW];
int index_fenetre = 0;

int PERTES_ADM;
int pertesANegocier;

void print_window(){
	// affichage de la fenetre :
	printf("fenetre :  {");
	for(int i=0 ; i<SIZE_WINDOW ; i++){
		printf("%d, ", (int)fenetre[i]);
	} 
	printf("}\n");
} 

int verif_taux_ok(){
	int nb_perdus = 0;
	for(int i=0 ; i<SIZE_WINDOW ; i++){
		nb_perdus += fenetre[i]; 
	} 
	return 100*nb_perdus/SIZE_WINDOW < PERTES_ADM;
} 

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
	printf("[MIC-TCP] Appel de la fonction: ");  
	printf(__FUNCTION__); 
	printf("\n");

	if (sm == CLIENT) {
		pertesANegocier = 10;
		printf("Coté client, on demande %d de pertes\n", pertesANegocier);
	}
	else {
		pertesANegocier = 15;
		printf("Coté serveur, on demande %d de pertes\n", pertesANegocier);
	}
   
	/* Appel obligatoire */   
	if (initialize_components(sm) == -1){ 
        perror("erreur de création de socket\n");
		return -1;
	}
	else {
		// Descripteur du socket
		sock.fd = 0;
		// Etat du socket
		sock.state = IDLE;
		// Permet de réguler le pourcentage de pertes
		set_loss_rate(TAUX_PERTES);
		return sock.fd;
	}
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
   printf("[MIC-TCP] Appel de la fonction: ");
   printf(__FUNCTION__); 
   printf("\n");
   
   // Il faut vérifier que c'est le bon socket
   if (sock.fd == socket){
	   // memcpy car c'est une copie d'une structure à une autre
	   memcpy((char*)&sock.addr,(char*)&addr, sizeof(mic_tcp_sock_addr));
	   return 0;
   }
   else {
	   return -1;
   }
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  
	printf(__FUNCTION__); 
	printf("\n");

	// Il faut vérifier que c'est le bon socket et que la connexion n'est pas fermée
	if ((sock.fd == socket) && (sock.state != CLOSED)){
		sock.state = ATTENTE_SYN;

		if (pthread_mutex_lock(&mutex) != 0) {
			printf("Erreur du lock\n");
		}

		while (sock.state == ATTENTE_SYN) {
			pthread_cond_wait(&cond, &mutex);
		}

		if (pthread_mutex_unlock(&mutex) != 0) {
			printf("Erreur du unlock\n");
		}
		
		//Construction SYNACK
		mic_tcp_pdu synack = {0};
		synack.header.source_port = sock.addr.port;
		// L'adresse de destination est addr
		synack.header.dest_port   = addr->port;
		synack.header.ack_num	  = pertesANegocier; //on envoie le taux qu'on veut
		synack.header.syn         = 1;	
		synack.header.ack		  = 1;

		//Envoi du synack
		if (IP_send(synack, *addr) == -1){
			printf("Erreur sur l'IP_send\n");
		}

		if (pthread_mutex_lock(&mutex) != 0) {
			printf("Erreur du lock\n");
		}

		while(sock.state == ATTENTE_ACK) {
			if (pthread_cond_wait(&cond, &mutex) != 0) {
				printf("Erreur du cond wait\n");
			}
		}

		if (pthread_mutex_unlock(&mutex) != 0) {
			printf("Erreur de l'unlock\n");
		}

		printf("Résultat négociations : %d\n", PERTES_ADM);

		return 0;
	}
	else {
		return -1;
	}
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
	printf("[MIC-TCP] Appel de la fonction: ");  
	printf(__FUNCTION__); 
	printf("\n");

	int nbEchecs = 0;
	
	// Il faut vérifier que c'est le bon socket et que la connexion n'est pas fermée
	if ((sock.fd == socket) && (sock.state != CLOSED)){

		//Construction SYN
		mic_tcp_pdu syn = {0};
		syn.header.source_port = sock.addr.port;
		// L'adresse de destination est addr
		syn.header.dest_port   = addr.port;
		syn.header.syn         = 1;	
		
		int synack_recu = 0;

		// on vérifie que le nb d'envois jusqu'ici n'est pas trop élevé
		while (!synack_recu && nbEchecs < MAX_ECHECS) {
			nbEchecs++;

			//Envoi du syn
			if (IP_send(syn, addr) == -1){
				printf("Erreur sur l'IP_send\n");
			}

			sock.state = ATTENTE_SYNACK;

			// Activation du timer
			mic_tcp_pdu synack_recv = {0};
			mic_tcp_sock_addr addr_dist = {0};

			if (IP_recv(&synack_recv, &addr_dist, TIMER) == -1) {
				printf("pb réception synack\n");
			}
			else {
				if (synack_recv.header.syn && synack_recv.header.ack) {
					synack_recu = 1;
					
					// Négociation des pertes, on prend le plus petit
					PERTES_ADM = pertesANegocier;
					int tauxPertesClient = synack_recv.header.ack_num;
					if (tauxPertesClient < pertesANegocier) {
						PERTES_ADM = tauxPertesClient;
					}

					printf("Résultat négociations : %d\n", PERTES_ADM);

					//Construction ack
					mic_tcp_pdu ack = {0};
					// Header de notre ACK
					ack.header.source_port = sock.addr.port;
					ack.header.dest_port   = addr.port;
					ack.header.ack_num	   = PERTES_ADM;
					ack.header.ack         = 1;	

					//Envoi du ack
					if (IP_send(ack, addr) == -1){
						printf("Erreur sur l'IP_send\n");
						return -1;
					}
				}
			}
		}

		if (nbEchecs == MAX_ECHECS) {
			printf("Trop d'échecs, on est faible et on abandonne\n");
			sock.state = CLOSING;
			return -1;
		}

		sock.state = ESTABLISHED;

		return 0;
	}
	else {
		return -1;
	}
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); 
	printf(__FUNCTION__); 
	printf("\n");
	
	mic_tcp_pdu PDU = {0};
	mic_tcp_pdu PDU_recv = {0};
	mic_tcp_sock_addr addr_dist = {0};
	int sent_size;
	int recv_size;
	
	if ((sock.fd == mic_sock) && (sock.state == ESTABLISHED)){
		// On construit le PDU : header et payload
		PDU.header.source_port = sock.addr.port;
		PDU.header.dest_port   = addr_socket_dest.port;
        PDU.header.seq_num     = PE;
		
		PDU.payload.data 	   = mesg;
		PDU.payload.size	   = mesg_size;
	
		numero_paquet++;
		PE = (PE+1)%2;

		sock.state = ATTENTE_ACK;

		int sent = 0; //boolean that checks whether the msg has correctly been sent or not

        while(!sent) {
			// envoi du PDU
			if ((sent_size = IP_send(PDU, addr_socket_dest)) == -1){
				printf("Erreur sur l'IP_send\n");
			} 

            // Activation du timer
			recv_size = IP_recv(&PDU_recv, &addr_dist, TIMER);
			//printf("PDU ack num %d et PE %d\n", PDU_recv.header.ack_num, PE);
			
			// PDU non reçu
            if (recv_size < 0) {
				if (!verif_taux_ok()) {
					// On renvoie le PDU en recommençant la boucle car le taux de perte est trop élevé
					//printf("Renvoi du paquet n°%d\n", numero_paquet);
				} else {
					// paquet perdu, perte acceptable
					//printf("On accepte la perte du paquet n°%d\n", numero_paquet);
					fenetre[index_fenetre] = 1;
					sent = 1;
				}
            }	

            // L'envoi a réussi
            else {
				if (PDU_recv.header.ack && PDU_recv.header.ack_num == PE) {
					//printf("envoi du paquet  n°%d\n", numero_paquet);
					fenetre[index_fenetre] = 0; 
					sent = 1;
				}
            }
        }
		
		//print_window();
		index_fenetre = (index_fenetre + 1) % SIZE_WINDOW;
        sock.state = ESTABLISHED;
		return sent_size;
	} 
	else {
		return -1;
	}
}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); 
	printf(__FUNCTION__); 
	printf("\n");
	
	mic_tcp_payload PDU = {0};
	int nbr_octets_lus = -1;
	
	// Payload
	PDU.data = mesg;
	PDU.size = max_mesg_size;
	
	if ((sock.fd == socket) && (sock.state == ESTABLISHED)){
		// Mise en attente d'un PDU par l'état IDLE
		sock.state = IDLE;
		
		// Retrait d'un PDU dans le buffer de réception
		nbr_octets_lus = app_buffer_get(PDU);
		
		// Etat connecté suite à la récupération du PDU
		sock.state = ESTABLISHED;
		
		return nbr_octets_lus;
	}
	else {
		return -1;
	}
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
    printf("[MIC-TCP] Appel de la fonction :  "); 
	printf(__FUNCTION__); 
	printf("\n\n");
	
	// Si c'est le bon socket et que la connexion est en cours
	if ((sock.fd == socket) && (sock.state == ESTABLISHED)){
		// Se mettre en état de connexion fermée
		sock.state = CLOSED;
		return 0;
	}
	else {
		return -1;
	}
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: "); 
	printf(__FUNCTION__); 
	printf("\n");
    
	//printf("PA vaut %d et seq_num vaut %d\n", PA, pdu.header.seq_num);
	
	if (sock.state == ATTENTE_SYN && pdu.header.syn) {
		sock.state = ATTENTE_ACK;
		if (pthread_cond_broadcast(&cond) != 0) {
			printf("Erreur broadcast\n");
		}
	}
	else if (sock.state == ATTENTE_ACK && pdu.header.ack) {
		sock.state = ESTABLISHED;
		if (pthread_cond_broadcast(&cond) != 0) {
			printf("Erreur broadcast\n");
		}
		PERTES_ADM = pdu.header.ack_num;
	}
	else {
		if (pdu.header.seq_num == PA) {
			// Insertion des données utiles (message + taille) du PDU dans le buffer de réception du socket
			app_buffer_put(pdu.payload);
			PA = (PA+1) %2;
		}
		
		mic_tcp_pdu ack = {0};
		// Header de notre ACK
		ack.header.source_port = sock.addr.port;
		ack.header.dest_port   = addr.port;
		ack.header.ack_num     = PA;
		ack.header.ack         = 1;	

		// Envoi
		IP_send(ack, addr);
		//printf("On est prêt à recevoir le PA n°%d\n", PA);
	}
}
