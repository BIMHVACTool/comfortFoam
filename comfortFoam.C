/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

Application
    comfortFoam

Description
    This tool calculates the following values:
    - Operative room air temperature (TOp)
    - DR  (Draugth Rating or Draft Risk), Range: 0 to 100% (DR)
    - PMV (Predicted Percentage of Dissatisfied), Range: 0 bis 100% (PMV)
    - PPD (Predicted Mean Vote), Range: -3 (cold) to 3 (hot) (PPD)
    - Air condition effectivity (AE)
    - Humidity, Range: 0 bis 100%
    - Average room temperature
    - Turbulent intensity %

Background
    DIN EN ISO 7730

Autor
    Thomas Tian

Version
    2.0

\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "singlePhaseTransportModel.H"
#include "wallFvPatch.H"
#include "cellSet.H"
#include "turbulentTransportModel.H"
#include "turbulentFluidThermoModel.H"
#include "radiationModel.H"

// * * * * * * * * * * * * * * * * Functions * * * * * * * * * * * * * * * * //

Foam::vector Uaverage
(
    const fvMesh& mesh_
)
{
    const volVectorField& U = mesh_.lookupObject<volVectorField>("U");
    vector midU(0,0,0);
    scalar volume(0);

    forAll (mesh_.cells(), cellI)
    {
        midU += U[cellI] * mesh_.V()[cellI];
        volume += mesh_.V()[cellI];
    };

    return midU/volume;
}


Foam::scalar Taverage
(
    const fvMesh& mesh_
)
{
    const volScalarField& T = mesh_.lookupObject<volScalarField>("T");
    scalar midT(0);
    scalar volume(0);

    forAll (mesh_.cells(), cellI)
    {
        midT += T[cellI] * mesh_.V()[cellI];
        volume += mesh_.V()[cellI];
    };

    return (midT/volume);
}


Foam::scalar radiationTemperature
(
    const Foam::fvMesh& mesh_,
    const Foam::fvPatchList& Patches_
)
{
    //- Calculate the radiation temperatur Umgebung
    //  radiation temperatur =
    //  Sum(mean wall surface temp / sum(patches))
    const volScalarField& T = mesh_.lookupObject<volScalarField>("T");
    scalar PatchSumTemp(0), area(0);
    int counter = 0;

    forAll (Patches_, patchI)
    {
        const label curPatch = Patches_[patchI].index();

        if (isType<wallFvPatch>( Patches_[patchI] ))
        {
            area = gSum(mesh_.magSf().boundaryField()[curPatch]);

            if (area != 0)
            {
                PatchSumTemp +=
                    gSum
                    (
                        mesh_.magSf().boundaryField()[curPatch]
                      * T.boundaryField()[curPatch]
                    ) / area;

                counter++;
            }
        }
    }

    return (PatchSumTemp / counter) - 273.15;
}


// * * * * * * * * * * * * * * * * * Program * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
    argList::noParallel();
    Foam::timeSelector::addOptions();
    #include "addTimeOptions.H"
    #include "setRootCase.H"
    #include "createTime.H"

    //- Get times list
    Foam::instantList timeDirs = Foam::timeSelector::select0(runTime, args);

    #include "createNamedMesh.H"


    forAll(timeDirs, timei)
    {
        runTime.setTime(timeDirs[timei], timei);


        Info<< "Time = " << runTime.timeName() << endl;

        #include "createFields.H"

        //- Operative room air temperature Top
        volScalarField T(THeader, mesh);
        const fvPatchList& Patches = T.mesh().boundary();

        scalar STemp(20);

        if (w.headerOk()==1)
        {
            RHswitch = true;
        }

        if (Qr.headerOk()==1)
        {
            scalar PatchSumTemp(0);

            const fvPatchList& Patches_ = Qr.mesh().boundary();

            forAll (Patches_, patchI)
            {
                const label curPatch = Patches_[patchI].index();

                if (isType<wallFvPatch>(Patches_[patchI]))
                {
                    PatchSumTemp +=
                        gSum
                        (
                            mesh.magSf().boundaryField()[curPatch]
                          * Qr.boundaryField()[curPatch]
                        );
                }
            }

            Info<< "Sum of all heat flows: "
                << PatchSumTemp-273.15
                << " °C" << endl;
        }
        else
        {
            STemp = radiationTemperature(mesh, Patches);
        }

        volVectorField U(UHeader, mesh);
        vector wl = Uaverage(mesh);
        scalar Tr = Taverage(mesh);
        volScalarField p_rgh(p_rghHeader, mesh);

        scalar cellsum(0);
        scalar P1(0), P2(0), P3(0), P4(0), P5(0), XN(0), XF(0), HCN(0);
        scalar HCF(0), HC(0), PA(0), FCL(0), EPS(0), ICL(0);
        scalar Tu(0), HL1(0), HL2(0), HL3(0), HL4(0), HL5(0);
        scalar HL6(0), TCL(0), TS(0), TCLA(0), midPMV(0), midPPD(0);
        scalar midDR(0), midTO(0), midRH(0), p_w(0), p_ws(0.01);

        scalar volume(0);

        //- Correcture factor -> DIN EN 7730;
        scalar a_korr = 0;

        int N;

        //- FANGER - Equation (PMV)
        //  Loop only over cell list
        forAll (mesh.cells(), cellI)
        {
            if (RHswitch)
            {
                //- Calculate die Raumluftfeuchte
                p_w = ((101325 + p_rgh[cellI])
                    * w[cellI])/(0.62198 + 0.37802 * w[cellI]);

                p_ws = Foam::exp(-0.58002206*Foam::pow(10.0,4)
                    * Foam::pow(T[cellI],-1)
                    + 0.13914993*10
                    - 0.48640239*Foam::pow(10.0,-1)*T[cellI]
                    + 0.41764768*Foam::pow(10.0,-4)*Foam::pow(T[cellI],2)
                    - 0.14452093*Foam::pow(10.0,-7)*Foam::pow(T[cellI],3)
                    + 0.65459673*10*Foam::log(T[cellI]));

                RH[cellI] = p_w/p_ws*100;

                PA = RH[cellI] * 10
                    //- water vapour pressure, Pa
                    * Foam::exp(16.6563-(4030.183/(T[cellI]-273.15+235)));

                midRH += RH[cellI] * mesh.V()[cellI];
            }
            else
            {
                PA = RH1 * 10
                    //- water vapour pressure, Pa
                    * Foam::exp(16.6563-(4030.183/(T[cellI]-273.15+235)));
                midRH += RH1 * mesh.V()[cellI];
            }

            //- Calculate turbulent intensity %
            //  Tu = sqrt ( 1/3 * (Ux'² + Uy'² + Uz'²
            if (mag(wl) >0)
            {
                Tu = Foam::sqrt(scalar(1)/scalar(3)
                    *(
                        Foam::pow(U[cellI].x(),2) +
                        Foam::pow(U[cellI].y(),2) +
                        Foam::pow(U[cellI].z(),2)
                     )) * 100;
            }
            else
            {
                Tu = 0;
            }

            //- Calculate DR-value
            //  if wl<0,05 m/s use wl=0,05 m/s!
            if (mag(U[cellI]) >= 0.05)
            {
                DR[cellI] =
                    (34-(T[cellI]-273.15))
                  * (Foam::pow(mag(U[cellI])-0.05,0.62)
                  * ((0.37*mag(U[cellI])*Tu)+3.14));
            }
            else
            {
                DR[cellI] =
                    (34-T[cellI]-273.15)
                  * Foam::pow(0.05,0.6223)
                  * ((0.37*0.05*Tu)+3.14);
            }

            if (DR[cellI] > 100)
            {
                DR[cellI] = 100;
            }

            if (DR[cellI] < 0)
            {
                DR[cellI] = 0;
            }

            ICL = 0.155 * clo;

            //- OK
            if (ICL < 0.078)
            {
                FCL = 1 + 1.29 * ICL;
            }
            //- Clothing area factor
            else
            {
                FCL = 1.05 + 0.645 * ICL;
            }

            //- heat transf. coeff. by forced convection
            HCF = 12.1 * Foam::sqrt(mag(U[cellI]));

            TCLA = T[cellI] + (35.5-(T[cellI]-273.15)) / (3.5*6.45*(ICL+0.1));

            //- Ok
            XN = TCLA / 100;
            P1 = ICL * FCL;
            P2 = P1 * 3.96;
            P3 = P1 * 100;
            P4 = P1 * T[cellI];
            P5 =
                308.7-0.028
              * (met*58.15-wme*58.15)
              + P2*Foam::pow((STemp + 273.0)/100,4);

            //- Changed Tian 06.10.2012
            //  XF = XN;
            XF = TCLA / 50;
            EPS = 0.0015;
            N=0;

            do
            {
                N++;
                //- stop criteria by iteration
                XF = (XF + XN)/2;

                HCF = 12.1 * Foam::sqrt(mag(U[cellI]));

                //- heat transf. coeff. by natural convection
                HCN = 2.38 * Foam::pow(mag(100*XF-T[cellI]),0.25);

                if (HCF>HCN)
                {
                    HC = HCF;
                }
                //- ok
                else
                {
                    HC = HCN;
                }

                XN = (P5+P4 * HC-P2 * Foam::pow(XF,4.0)) / (100 + P3 * HC);

                if (N>150)
                {
                    break;
                }

             } while (mag(XN-XF)>EPS);

            //- surface temperature of clothing
            TCL = 100*XN - 273;

            //- heat loss diff. through skin
            HL1 = 3.05*0.001*(5733-(6.99*(met*58.15-wme*58.15))-PA);

            if ((met*58.15-wme*58.15) > 58.15)
            {
                HL2 = 0.42*((met*58.15-wme*58.15)-58.15);
            }
            // heat loss by sweating (comfort)
            else
            {
                HL2 = 0;
            }


            //- latent respiration heat loss
            HL3 = 1.7*0.00001*met*58.15*(5867-PA);

            //- dry respiration heat loss
            HL4 = 0.0014*met*58.15*(34-(T[cellI]-273.15));

            //- heat lose by radiation
            HL5 = 3.96*FCL*(Foam::pow(XN,4)-Foam::pow(((STemp+273.0)/100),4));

            //- heat lose by convection
            HL6 = FCL*HC*(TCL-(T[cellI]-273.15));

            //- thermal sensation trans coeff
            TS = 0.303*Foam::exp(-0.036*met*58.15)+0.028;

            //- Calculate PMV-value
            PMV[cellI] = TS*((met*58.15-wme*58.15)-HL1-HL2-HL3-HL4-HL5-HL6);

            //Info << "100-95*exp(-0.03353*pow("<< PMV[cellI] << ", 4)"
            //     << "- 0.2179*pow(" << PMV[cellI] << ", 2)" << endl;
            //- Calculate PPD-value
            PPD[cellI] =
                100-95
              * Foam::exp
                    (
                        -0.03353*pow(PMV[cellI], 4)
                       - 0.2179*pow(PMV[cellI], 2)
                    );

            if (mag(U[cellI])<0.2)
            {
                a_korr=0.5;
            }

            if ((mag(U[cellI])>=0.2) || (mag(U[cellI])<=0.6))
            {
                a_korr=0.6;
            }

            if (mag(U[cellI])>0.6)
            {
                a_korr=0.7;
            }

            //- Calculate the operative room air temperature
            TOp[cellI] = a_korr*T[cellI]+(1-a_korr)*(STemp+273.15);

            midPMV += PMV[cellI]*mesh.V()[cellI];
            midPPD += PPD[cellI]*mesh.V()[cellI];
            midDR += DR[cellI]*mesh.V()[cellI];
            midTO += TOp[cellI]*mesh.V()[cellI];

            cellsum++;
            volume += mesh.V()[cellI];

        //- End forAll loop
        }

        DR.write();
        PMV.write();
        PPD.write();
        TOp.write();
        RH.write();

        Info<< "Mean Radiation temperature " << STemp << " °C" << nl
            << "Water vapour pressure " << PA << " Pa" <<nl
            << "Average PMV-Value = " << (midPMV/volume) << nl
            << "Average PPD-Value = " << (midPPD/volume) << " %" << nl
            << "Average DR-Value = " << (midDR/volume) << " %" << nl
            << "Average air velocity = " << mag(wl) << " m/s" << nl
            << "Average room temperature = " << mag(Tr)-273.15 << " °C" << nl
            << "Average relative room humidity = " << mag(midRH/volume) << " %"
            << nl
            << "Average operative temperature = " << (midTO/volume)-273.15
            << " °C" << nl
            << "Turbulence = "<< Tu << " %" << endl;

        if
        (
            ((midPMV/volume > -0.2) || (midPMV/volume < 0.2))
         && (midDR/volume < 10)
         && (midPPD/volume < 6)
        )
        {
            Info<< "Analysis: Category A" << endl;
        }
        else
        {
            if
            (
                ((midPMV/volume > -0.5) || (midPMV/volume < 0.5))
             && (midDR/volume < 20)
             && (midPPD/volume < 10)
            )
            {
                Info << "Analysis: Category B" << endl;
            }
            else
            {
                if
                (
                    ((midPMV/volume > -0.7) || (midPMV/volume < 0.7))
                 && (midDR/volume < 30)
                 && (midPPD/volume < 15)
                )
                {
                    Info << "Analysis: Category C" << endl;
                }
            }
        }
}
    return 0;
}


// ************************************************************************* //

