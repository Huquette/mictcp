#include <mictcp.h>
#include <../include/api/mictcp_core.h> 

#define TAUX_PERTES 30
#define PERTES_ADM 20
#define TIMER 10
#define SIZE_WINDOW 10 // taille de la fenetre glissante


// Variables globales :
mic_tcp_sock sock;
mic_tcp_sock_addr addr_socket_dest;
int PE = 0; // prochaine trame à émettre
int PA = 0; // prochaine trame attendue
int numero_paquet = 0;

/* creation de la fenetre glissante
le tableau sera initialisé avec que des 0 */
int fenetre[SIZE_WINDOW];
int index_fenetre = 0;

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
	int result = -1;
	printf("[MIC-TCP] Appel de la fonction: ");  
	printf(__FUNCTION__); 
	printf("\n");
   
	/* Appel obligatoire */   
	if ((result = initialize_components(sm)) == -1){ 
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
	   //PA = 1;
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
		// Connexion établie
		sock.state = ESTABLISHED; 
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
	
	// Il faut vérifier que c'est le bon socket et que la connexion n'est pas fermée
	if ((sock.fd == socket) && (sock.state != CLOSED)){
		// Connexion établie
		sock.state = ESTABLISHED; 
		// L'adresse de destination est addr
		addr_socket_dest = addr;
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
	
	mic_tcp_pdu PDU, PDU_recv;
	mic_tcp_sock_addr addr_dist;
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
			printf("PDU ack num %d et PE %d\n", PDU_recv.header.ack_num, PE);
			
			// PDU non reçu
            if (recv_size < 0) {
				if (!verif_taux_ok()) {
					// On renvoie le PDU en recommençant la boucle car le taux de perte est trop élevé
					printf("Renvoi du paquet n°%d\n", numero_paquet);
				} else {
					// paquet perdu, perte acceptable
					printf("On accepte la perte du paquet n°%d\n", numero_paquet);
					fenetre[index_fenetre] = 1;
					sent = 1;
				}
            }	

            // L'envoi a réussi
            else {
				if (PDU_recv.header.ack && PDU_recv.header.ack_num == PE) {
					printf("envoi du paquet  n°%d\n", numero_paquet);
					fenetre[index_fenetre] = 0; 
					sent = 1;
				}
            }
        }
		
		print_window();
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
	
	mic_tcp_payload PDU;
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
    
	printf("PA vaut %d et seq_num vaut %d\n", PA, pdu.header.seq_num);
	
	if (pdu.header.seq_num == PA) {
        // Insertion des données utiles (message + taille) du PDU dans le buffer de réception du socket
        app_buffer_put(pdu.payload);
		PA = (PA+1) %2;
    }
	
	mic_tcp_pdu ack;
	// Header de notre ACK
	ack.header.source_port = sock.addr.port;
  	ack.header.dest_port   = addr.port;
	ack.header.ack_num     = PA;
	ack.header.ack         = 1;	

	// Envoi
	IP_send(ack, addr);
	printf("On est prêt à recevoir le PA n°%d\n", PA);
}
