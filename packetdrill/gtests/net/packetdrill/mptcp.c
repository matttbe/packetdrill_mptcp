/**
 * Authors: Arnaud Schils
 */

#include "mptcp.h"

void init_mp_state()
{
	mp_state.packetdrill_key_set = false;
	mp_state.kernel_key_set = false;
	queue_init(&mp_state.vars_queue);
	mp_state.vars = NULL; //Init hashmap
}

void free_mp_state(){
	free_var_queue();
	free_vars();
	free_flows();
}

/**
 * Remember mptcp connection key generated by packetdrill. This key is needed
 * during the entire mptcp connection and is common among all mptcp subflows.
 */
void set_packetdrill_key(u64 sender_key)
{
	mp_state.packetdrill_key = sender_key;
	mp_state.packetdrill_key_set = true;
}

/**
 * Remember mptcp connection key generated by kernel. This key is needed
 * during the entire mptcp connection and is common among all mptcp subflows.
 */
void set_kernel_key(u64 receiver_key)
{
    mp_state.kernel_key = receiver_key;
    mp_state.kernel_key_set = true;
}

/* var_queue functions */

/**
 * Insert a COPY of name char* in mp_state.vars_queue.
 * Error is returned if queue is full.
 *
 */
int enqueue_var(char *name)
{
	unsigned name_length = strlen(name);
	char *new_el = malloc(sizeof(char)*name_length);
	memcpy(new_el, name, name_length);
	int full_err = queue_enqueue(&mp_state.vars_queue, new_el);
	return full_err;
}

//Caller should free
int dequeue_var(char **name){
	int empty_err = queue_dequeue(&mp_state.vars_queue, (void**)name);
	return empty_err;
}

//Free all variables names (char*) in vars_queue
void free_var_queue()
{
	queue_free(&mp_state.vars_queue);
}

/* hashmap functions */

/**
 *
 * Save a variable <name, value> in variables hashmap.
 * Where value is of u64 type key.
 *
 * Key memory location should stay valid, name is copied.
 *
 */
void add_mp_var_key(char *name, u64 *key)
{
	struct mp_var *var = malloc(sizeof(struct mp_var));
	unsigned name_length = strlen(name);
	var->name = malloc(sizeof(char)*(name_length+1));
	memcpy(var->name, name, (name_length+1)); //+1 to copy '/0' too
	var->value = key;
	var->type = KEY;
	add_mp_var(var);
}

/**
 * Add var to the variable hashmap.
 */
void add_mp_var(struct mp_var *var)
{
	HASH_ADD_KEYPTR(hh, mp_state.vars, var->name, strlen(var->name), var);
}

/**
 * Search in the hashmap for the value of the variable of name "name" and
 * return both variable - value (mp_var struct).
 * NULL is returned if not found
 */
struct mp_var *find_mp_var(char *name)
{
    struct mp_var *found;
    HASH_FIND_STR(mp_state.vars, name, found);
    return found;
}

/**
 * Gives next mptcp key value needed to insert variable values while processing
 * the packets.
 */
u64 *find_next_key(){
	char *var_name;
	if(dequeue_var(&var_name) || !var_name){
		return NULL;
	}

	struct mp_var *var = find_mp_var(var_name);
	free(var_name);

	if(!var || var->type != KEY){
		return NULL;
	}
	return (u64*)var->value;
}


/**
 * Iterate through hashmap, free mp_var structs and mp_var->name,
 * value is not freed since values come from stack.
 */
void free_vars()
{
	struct mp_var *next, *var;
	var = mp_state.vars;

	while(var){
		next = var->hh.next;
		free(var->name);
		free(var);
		var = next;
	}
}

/**
 * @pre inbound packet should be the first packet of a three-way handshake
 * mp_join initiated by packetdrill (thus an inbound mp_join syn packet).
 *
 * @post
 * - Create a new subflow structure containing all available information at this
 * time (src_ip, dst_ip, src_port, dst_port, packetdrill_rand_nbr,
 * packetdrill_addr_id). kernel_addr_id and kernel_rand_nbr should be set when
 * receiving syn+ack with mp_join mptcp option from kernel.
 *
 * - last_packetdrill_addr_id is incremented.
 */
struct mp_subflow *new_subflow_inbound(struct packet *inbound_packet)
{

	struct mp_subflow *subflow = malloc(sizeof(struct mp_subflow));

	if(inbound_packet->ipv4){
		ip_from_ipv4(&inbound_packet->ipv4->src_ip, &subflow->src_ip);
		ip_from_ipv4(&inbound_packet->ipv4->dst_ip, &subflow->dst_ip);
	}

	else if(inbound_packet->ipv6){
		ip_from_ipv6(&inbound_packet->ipv6->src_ip, &subflow->src_ip);
		ip_from_ipv6(&inbound_packet->ipv6->dst_ip, &subflow->dst_ip );
	}

	else{
		return NULL;
	}

	subflow->src_port =	inbound_packet->tcp->src_port;
	subflow->dst_port = inbound_packet->tcp->dst_port;
	subflow->packetdrill_rand_nbr =	generate_32();
	subflow->packetdrill_addr_id = mp_state.last_packetdrill_addr_id;
	mp_state.last_packetdrill_addr_id++;
	subflow->next = mp_state.subflows;
	mp_state.subflows = subflow;

	return subflow;
}

struct mp_subflow *new_subflow_outound(struct packet *outbound_packet)
{

	struct mp_subflow *subflow = malloc(sizeof(struct mp_subflow));
	struct tcp_option *mp_join_syn =
			get_tcp_option(outbound_packet, TCPOPT_MPTCP);

	if(!mp_join_syn)
		return NULL;

	if(outbound_packet->ipv4){
		ip_from_ipv4(&outbound_packet->ipv4->dst_ip, &subflow->src_ip);
		ip_from_ipv4(&outbound_packet->ipv4->src_ip, &subflow->dst_ip);
	}

	else if(outbound_packet->ipv6){
		ip_from_ipv6(&outbound_packet->ipv6->dst_ip, &subflow->src_ip);
		ip_from_ipv6(&outbound_packet->ipv6->src_ip, &subflow->dst_ip);
	}

	else{
		return NULL;
	}

	subflow->src_port =	outbound_packet->tcp->dst_port;
	subflow->dst_port = outbound_packet->tcp->src_port;
	subflow->kernel_rand_nbr =
			mp_join_syn->data.mp_join_syn.sender_random_number;
	subflow->kernel_addr_id =
			mp_join_syn->data.mp_join_syn.address_id;
	subflow->next = mp_state.subflows;
	mp_state.subflows = subflow;

	return subflow;
}

/**
 * Return the first subflow S of mp_state.subflows for which match(packet, S)
 * returns true.
 */
struct mp_subflow *find_matching_subflow(struct packet *packet,
		bool (*match)(struct mp_subflow*, struct packet*))
{
	struct mp_subflow *subflow = mp_state.subflows;
	while(subflow){
		if((*match)(subflow, packet)){
			return subflow;
		}
		subflow = subflow->next;
	}
	return NULL;
}

static bool does_subflow_match_outbound_packet(struct mp_subflow *subflow,
		struct packet *outbound_packet){
	return subflow->dst_port == outbound_packet->tcp->src_port &&
			subflow->src_port == outbound_packet->tcp->dst_port;
}

struct mp_subflow *find_subflow_matching_outbound_packet(
		struct packet *outbound_packet)
{
	return find_matching_subflow(outbound_packet, does_subflow_match_outbound_packet);
}

static bool does_subflow_match_inbound_packet(struct mp_subflow *subflow,
		struct packet *inbound_packet){
	return subflow->dst_port == inbound_packet->tcp->dst_port &&
			subflow->src_port == inbound_packet->tcp->src_port;
}

struct mp_subflow *find_subflow_matching_inbound_packet(
		struct packet *inbound_packet)
{
	return find_matching_subflow(inbound_packet, does_subflow_match_inbound_packet);
}

struct mp_subflow *find_subflow_matching_socket(struct socket *socket){
	struct mp_subflow *subflow = mp_state.subflows;
	while(subflow){
		if(subflow->dst_port == socket->live.remote.port &&
				subflow->src_port == socket->live.local.port){
			return subflow;
		}
		subflow = subflow->next;
	}
	return NULL;
}

/**
 * Free all mptcp subflows struct being a member of mp_state.subflows list.
 */
void free_flows(){
	struct mp_subflow *subflow = mp_state.subflows;
	struct mp_subflow *temp;
	while(subflow){
		temp = subflow->next;
		free(subflow);
		subflow = temp;
	}
}

/**
 * Generate a mptcp packetdrill side key and save it for later reference in
 * the script.
 */
int mptcp_gen_key()
{

	//Retrieve variable name parsed by bison.
	char *snd_var_name;
	if(queue_front(&mp_state.vars_queue, (void**)&snd_var_name)){
		return STATUS_ERR;
	}

	//First inbound mp_capable, generate new key
	//and save corresponding variable
	if(!mp_state.packetdrill_key_set){
		seed_generator();
		u64 key = generate_key64();
		set_packetdrill_key(key);
		add_mp_var_key(snd_var_name, &mp_state.packetdrill_key);
	}

	return STATUS_OK;
}

/**
 * Insert key field value of mp_capable_syn mptcp option according to variable
 * specified in user script.
 *
 */
int mptcp_set_mp_cap_syn_key(struct tcp_option *tcp_opt)
{
	u64 *key = find_next_key();
	if(!key)
		return STATUS_ERR;
	tcp_opt->data.mp_capable_syn.key = *key;
	return STATUS_OK;
}

/**
 * Insert keys fields values of mp_capable mptcp option according to variables
 * specified in user script.
 */
int mptcp_set_mp_cap_keys(struct tcp_option *tcp_opt)
{
	u64 *key = find_next_key();
	if(!key)
		return STATUS_ERR;
	tcp_opt->data.mp_capable.sender_key = *key;

	key = find_next_key();
	if(!key)
		return STATUS_ERR;
	tcp_opt->data.mp_capable.receiver_key = *key;
	return STATUS_OK;
}

/**
 * Extract mptcp connection informations from mptcp packets sent by kernel.
 * (For example kernel mptcp key).
 */
static int extract_and_set_kernel_key(
		struct packet *live_packet)
{

	struct tcp_option* mpcap_opt =
			get_tcp_option(live_packet, TCPOPT_MPTCP);

	if(!mpcap_opt)
		return STATUS_ERR;


	//12 bytes MPTCP capable option?
	if(!mp_state.kernel_key_set){

		//Set found kernel key
		set_kernel_key(mpcap_opt->data.mp_capable_syn.key);
		//Set front queue variable name to refer to kernel key
		char *var_name;
		if(queue_front(&mp_state.vars_queue, (void**)&var_name)){
			return STATUS_ERR;
		}
		add_mp_var_key(var_name, &mp_state.kernel_key);
	}

	return STATUS_OK;
}


/**
 * Insert appropriate key in mp_capable mptcp option.
 */
int mptcp_subtype_mp_capable(struct packet *packet_to_modify,
		struct packet *live_packet,
		struct tcp_option *tcp_opt_to_modify,
		unsigned direction)
{
	int error;
	if(tcp_opt_to_modify->length == TCPOLEN_MP_CAPABLE_SYN &&
			packet_to_modify->tcp->syn &&
			direction == DIRECTION_INBOUND &&
			!packet_to_modify->tcp->ack){
		error = mptcp_gen_key();
		error = mptcp_set_mp_cap_syn_key(tcp_opt_to_modify) || error;
	}

	else if(tcp_opt_to_modify->length == TCPOLEN_MP_CAPABLE_SYN &&
			packet_to_modify->tcp->syn &&
			direction == DIRECTION_OUTBOUND){
		error = extract_and_set_kernel_key(live_packet);
		error = mptcp_set_mp_cap_syn_key(tcp_opt_to_modify);
	}

	else if(tcp_opt_to_modify->length == TCPOLEN_MP_CAPABLE &&
			!packet_to_modify->tcp->syn &&
			packet_to_modify->tcp->ack){
		error = mptcp_set_mp_cap_keys(tcp_opt_to_modify);
	}

	else if(tcp_opt_to_modify->length == TCPOLEN_MP_CAPABLE_SYN &&
			packet_to_modify->tcp->syn &&
			direction == DIRECTION_INBOUND &&
			packet_to_modify->tcp->ack){
		error = mptcp_gen_key();
		error = mptcp_set_mp_cap_syn_key(tcp_opt_to_modify) || error;
	}

	else{
		return STATUS_ERR;
	}
	return error;
}

/**
 * Update mptcp subflows state according to sent/sniffed mp_join packets.
 * Insert appropriate values retrieved from this up-to-date state in inbound
 * and outbound packets.
 */
int mptcp_subtype_mp_join(struct packet *packet_to_modify,
						struct packet *live_packet,
						struct tcp_option *tcp_opt_to_modify,
						unsigned direction)
{

	if(direction == DIRECTION_INBOUND &&
			!packet_to_modify->tcp->ack &&
			packet_to_modify->tcp->syn &&
			tcp_opt_to_modify->length == TCPOLEN_MP_JOIN_SYN){

		struct mp_subflow *subflow = new_subflow_inbound(packet_to_modify);
		if(!subflow)
			return STATUS_ERR;

		tcp_opt_to_modify->data.mp_join_syn.receiver_token =
				get_token_32(mp_state.kernel_key);
		tcp_opt_to_modify->data.mp_join_syn.sender_random_number =
				subflow->packetdrill_rand_nbr;
		tcp_opt_to_modify->data.mp_join_syn.address_id = subflow->packetdrill_addr_id;
	}

	else if(direction == DIRECTION_OUTBOUND &&
			packet_to_modify->tcp->ack &&
			packet_to_modify->tcp->syn &&
			tcp_opt_to_modify->length == TCPOLEN_MP_JOIN_SYN_ACK){

		struct mp_subflow *subflow =
				find_subflow_matching_outbound_packet(live_packet);
		struct tcp_option *live_mp_join =
				get_tcp_option(live_packet, TCPOPT_MPTCP);

		if(!subflow || !live_mp_join)
			return STATUS_ERR;

		//Update mptcp packetdrill state
		subflow->kernel_addr_id =
				live_mp_join->data.mp_join_syn_ack.address_id;
		subflow->kernel_rand_nbr =
				live_mp_join->data.mp_join_syn_ack.sender_random_number;

		//Build key for HMAC-SHA1
		unsigned char hmac_key[16];
		unsigned long *key_b = (unsigned long*)hmac_key;
		unsigned long *key_a = (unsigned long*)&(hmac_key[8]);
		*key_b = mp_state.kernel_key;
		*key_a = mp_state.packetdrill_key;

		//Build message for HMAC-SHA1
		unsigned msg[2];
		msg[0] = subflow->kernel_rand_nbr;
		msg[1] = subflow->packetdrill_rand_nbr;

		//Update script packet mp_join option fields
		tcp_opt_to_modify->data.mp_join_syn_ack.address_id =
				live_mp_join->data.mp_join_syn_ack.address_id;
		tcp_opt_to_modify->data.mp_join_syn_ack.sender_random_number =
				live_mp_join->data.mp_join_syn_ack.sender_random_number;
		tcp_opt_to_modify->data.mp_join_syn_ack.sender_hmac =
				hmac_sha1_truncat_64(hmac_key,
						16,
						(char*)msg,
						8);
	}

	else if(direction == DIRECTION_INBOUND &&
			packet_to_modify->tcp->ack &&
			!packet_to_modify->tcp->syn &&
			tcp_opt_to_modify->length == TCPOLEN_MP_JOIN_ACK){

		struct mp_subflow *subflow = find_subflow_matching_inbound_packet(packet_to_modify);

		if(!subflow)
			return STATUS_ERR;

		//Build HMAC-SHA1 key
		unsigned char hmac_key[16];
		unsigned long *key_a = (unsigned long*)hmac_key;
		unsigned long *key_b = (unsigned long*)&(hmac_key[8]);
		*key_a = mp_state.packetdrill_key;
		*key_b = mp_state.kernel_key;

		//Build HMAC-SHA1 message
		unsigned msg[2];
		msg[0] = subflow->packetdrill_rand_nbr;
		msg[1] = subflow->kernel_rand_nbr;

		u32 sender_hmac[5];
		hmac_sha1(hmac_key,
				16,
				(char*)msg,
				8,
				sender_hmac);

		//Inject correct mp_join fields to be sent to kernel
		memcpy(tcp_opt_to_modify->data.mp_join_ack.sender_hmac,
				sender_hmac,
				20);
	}

	else if(direction == DIRECTION_OUTBOUND &&
			!packet_to_modify->tcp->ack &&
			packet_to_modify->tcp->syn &&
			tcp_opt_to_modify->length == TCPOLEN_MP_JOIN_SYN){

		struct mp_subflow *subflow = new_subflow_outound(live_packet);

		tcp_opt_to_modify->data.mp_join_syn.address_id =
				subflow->kernel_addr_id;
		tcp_opt_to_modify->data.mp_join_syn.sender_random_number =
				subflow->kernel_rand_nbr;
		tcp_opt_to_modify->data.mp_join_syn.receiver_token =
				get_token_32(mp_state.kernel_key);
	}

	else if(direction == DIRECTION_INBOUND &&
			packet_to_modify->tcp->ack &&
			packet_to_modify->tcp->syn &&
			tcp_opt_to_modify->length == TCPOLEN_MP_JOIN_SYN_ACK){

		struct mp_subflow *subflow =
				find_subflow_matching_inbound_packet(packet_to_modify);

		if(!subflow)
			return STATUS_ERR;

		subflow->packetdrill_rand_nbr = generate_32();

		//Build key for HMAC-SHA1
		unsigned char hmac_key[16];
		unsigned long *key_a = (unsigned long*)hmac_key;
		unsigned long *key_b = (unsigned long*)&(hmac_key[8]);
		*key_a = mp_state.packetdrill_key;
		*key_b = mp_state.kernel_key;

		//Build message for HMAC-SHA1
		unsigned msg[2];
		msg[0] = subflow->packetdrill_rand_nbr;
		msg[1] = subflow->kernel_rand_nbr;

		tcp_opt_to_modify->data.mp_join_syn_ack.address_id =
				mp_state.last_packetdrill_addr_id;
		mp_state.last_packetdrill_addr_id++;
		tcp_opt_to_modify->data.mp_join_syn_ack.sender_random_number =
				subflow->packetdrill_rand_nbr;

		tcp_opt_to_modify->data.mp_join_syn_ack.sender_hmac =
				hmac_sha1_truncat_64(hmac_key,
						16,
						(char*)msg,
						8);
	}

	else if(direction == DIRECTION_OUTBOUND &&
			packet_to_modify->tcp->ack &&
			!packet_to_modify->tcp->syn &&
			tcp_opt_to_modify->length == TCPOLEN_MP_JOIN_ACK){

		struct mp_subflow *subflow =
				find_subflow_matching_outbound_packet(packet_to_modify);

		if(!subflow)
			return STATUS_ERR;


		//Build HMAC-SHA1 key
		unsigned char hmac_key[16];
		unsigned long *key_b = (unsigned long*)hmac_key;
		unsigned long *key_a = (unsigned long*)&(hmac_key[8]);
		*key_b = mp_state.kernel_key;
		*key_a = mp_state.packetdrill_key;

		//Build HMAC-SHA1 message
		unsigned msg[2];
		msg[0] = subflow->kernel_rand_nbr;
		msg[1] = subflow->packetdrill_rand_nbr;

		u32 sender_hmac[5];
		hmac_sha1(hmac_key,
				16,
				(char*)msg,
				8,
				sender_hmac);

		memcpy(tcp_opt_to_modify->data.mp_join_ack.sender_hmac,
				sender_hmac,
				20);
	}

	else{
		return STATUS_ERR;
	}
	return STATUS_OK;
}

/**
 * Main function for managing mptcp packets. We have to insert appropriate
 * fields values for mptcp options according to previous state.
 *
 * Some of these values are generated randomly (packetdrill mptcp key,...)
 * others are sniffed from packets sent by the kernel (kernel mptcp key,...).
 * These values have to be inserted some mptcp script and live packets.
 */
int mptcp_insert_and_extract_opt_fields(struct packet *packet_to_modify,
		struct packet *live_packet, // could be the same as packet_to_modify
		unsigned direction)
{

	struct tcp_options_iterator tcp_opt_iter;
	struct tcp_option *tcp_opt_to_modify =
			tcp_options_begin(packet_to_modify, &tcp_opt_iter);
	int error;

	while(tcp_opt_to_modify != NULL){

		if(tcp_opt_to_modify->kind == TCPOPT_MPTCP){

			switch(tcp_opt_to_modify->data.mp_capable.subtype){

			case MP_CAPABLE_SUBTYPE:
				error = mptcp_subtype_mp_capable(packet_to_modify,
						live_packet,
						tcp_opt_to_modify,
						direction);
				break;

			case MP_JOIN_SUBTYPE:
				error = mptcp_subtype_mp_join(packet_to_modify,
						live_packet,
						tcp_opt_to_modify,
						direction);
				break;

			default:
				error =  STATUS_ERR;
			}

			if(error)
				return STATUS_ERR;

		}
		tcp_opt_to_modify = tcp_options_next(&tcp_opt_iter, NULL);
	}

	return STATUS_OK;
}