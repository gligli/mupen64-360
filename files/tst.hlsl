int4 main(): COLOR {
	int i;
	int j;
	int k;
	int toto=0;

	[loop]
	for(i=0;i<10;++i){
		toto+=14;		
	}

	[loop]
	for(j=0;j<0x42;++j){
		toto-=14;		
	}

	[loop]
	for(j=0;j<0x22;++j){
		toto*=14;		
	}

	return int4(toto,23,0,toto);
}
