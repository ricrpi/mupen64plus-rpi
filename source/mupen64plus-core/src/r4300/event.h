
int Event_Send(unsigned int Flags);
int Event_ReceiveAll(unsigned int Flags);
int Event_ReceiveAny(unsigned int Flags, unsigned int * Found);
int Event_ReceiveAnyNB(unsigned int Flags, unsigned int * Found);
int Event_Peek(unsigned int * Found);
