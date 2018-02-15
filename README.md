# comfortFoam
comfortFoam Tool for OpenFOAM

If you like to compiling under dev Version than you need to change the "Qr" to "qr" in:

    volScalarField Qr
    (
	IOobject
	(
        	"Qr",   // <---- for dev small qr
        	runTime.timeName(),
        	mesh,
        	IOobject::READ_IF_PRESENT,
        	IOobject::NO_WRITE
    	),
        mesh,
        dimensionedScalar("Qr", dimensionSet(0,0,0,0,0,0,0), 0.0)
    );
