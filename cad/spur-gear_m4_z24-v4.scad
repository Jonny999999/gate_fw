// Spiel zwischen Zahnflanken
spiel = 0.1;


// Höhe des Zahnkopfes über dem Teilkreis
modul=4;


// Anzahl der Radzähne
zahnzahl=24;


// Zahnbreite default:10
// komplett bis motor breite=29+6.5;
breite=27;

// Durchmesser der Mittelbohrung 
bohrung=16.3;


// Eingriffswinkel, Standardwert = 20° gemäß DIN 867
eingriffswinkel=20;


// Schrägungswinkel zur Rotationsachse; 0° = Geradverzahnung default:15
schraegungswinkel=0;


// Löcher zur Material-/Gewichtsersparnis bzw. Oberflächenvergößerung erzeugen, wenn Geometrie erlaubt
optimiert = 0;


/* Bibliothek für Evolventen-Zahnräder für Thingiverse Customizer

Enthält die Module
stirnrad(modul, zahnzahl, breite, bohrung, eingriffswinkel = 20, schraegungswinkel = 0, optimiert = true)

Autor:		Dr Jörg Janssen
Stand:		6. Januar 2017
Version:	2.0
Lizenz:		Creative Commons - Attribution, Non Commercial, Share Alike

Erlaubte Module nach DIN 780:
0.05 0.06 0.08 0.10 0.12 0.16
0.20 0.25 0.3  0.4  0.5  0.6
0.7  0.8  0.9  1    1.25 1.5
2    2.5  3    4    5    6
8    10   12   16   20   25
32   40   50   60

*/


/* [Hidden] */
pi = 3.14159;
rad = 57.29578;
$fn = 96;

/*	Wandelt Radian in Grad um */
function grad(eingriffswinkel) =  eingriffswinkel*rad;

/*	Wandelt Grad in Radian um */
function radian(eingriffswinkel) = eingriffswinkel/rad;

/*	Wandelt 2D-Polarkoordinaten in kartesische um
    Format: radius, phi; phi = Winkel zur x-Achse auf xy-Ebene */
function pol_zu_kart(polvect) = [
	polvect[0]*cos(polvect[1]),  
	polvect[0]*sin(polvect[1])
];

/*	Kreisevolventen-Funktion:
    Gibt die Polarkoordinaten einer Kreisevolvente aus
    r = Radius des Grundkreises
    rho = Abrollwinkel in Grad */
function ev(r,rho) = [
	r/cos(rho),
	grad(tan(rho)-radian(rho))
];

/*  Wandelt Kugelkoordinaten in kartesische um
    Format: radius, theta, phi; theta = Winkel zu z-Achse, phi = Winkel zur x-Achse auf xy-Ebene */
function kugel_zu_kart(vect) = [
	vect[0]*sin(vect[1])*cos(vect[2]),  
	vect[0]*sin(vect[1])*sin(vect[2]),
	vect[0]*cos(vect[1])
];


/*  Stirnrad
    modul = Höhe des Zahnkopfes über dem Teilkreis
    zahnzahl = Anzahl der Radzähne
    breite = Zahnbreite
    bohrung = Durchmesser der Mittelbohrung
    eingriffswinkel = Eingriffswinkel, Standardwert = 20° gemäß DIN 867. Sollte nicht größer als 45° sein.
    schraegungswinkel = Schrägungswinkel zur Rotationsachse; 0° = Geradverzahnung
	optimiert = Löcher zur Material-/Gewichtsersparnis bzw. Oberflächenvergößerung erzeugen, wenn Geometrie erlaubt (= 1, wenn wahr) */
module stirnrad(modul, zahnzahl, breite, bohrung, eingriffswinkel = 20, schraegungswinkel = 0, optimiert = true) {

	// Dimensions-Berechnungen	
	d = modul * zahnzahl;											// Teilkreisdurchmesser
	r = d / 2;														// Teilkreisradius
	alpha_stirn = atan(tan(eingriffswinkel)/cos(schraegungswinkel));// Schrägungswinkel im Stirnschnitt
	db = d * cos(alpha_stirn);										// Grundkreisdurchmesser
	rb = db / 2;													// Grundkreisradius
	da = (modul <1)? d + modul * 2.2 : d + modul * 2;				// Kopfkreisdurchmesser nach DIN 58400 bzw. DIN 867
	ra = da / 2;													// Kopfkreisradius
	c =  (zahnzahl <3)? 0 : modul/6;								// Kopfspiel
	df = d - 2 * (modul + c);										// Fußkreisdurchmesser
	rf = df / 2;													// Fußkreisradius
	rho_ra = acos(rb/ra);											// maximaler Abrollwinkel;
																	// Evolvente beginnt auf Grundkreis und endet an Kopfkreis
	rho_r = acos(rb/r);												// Abrollwinkel am Teilkreis;
																	// Evolvente beginnt auf Grundkreis und endet an Kopfkreis
	phi_r = grad(tan(rho_r)-radian(rho_r));							// Winkel zum Punkt der Evolvente auf Teilkreis
	gamma = rad*breite/(r*tan(90-schraegungswinkel));				// Torsionswinkel für Extrusion
	schritt = rho_ra/16;											// Evolvente wird in 16 Stücke geteilt
	tau = 360/zahnzahl;												// Teilungswinkel
	
	r_loch = (2*rf - bohrung)/8;									// Radius der Löcher für Material-/Gewichtsersparnis
	rm = bohrung/2+2*r_loch;										// Abstand der Achsen der Löcher von der Hauptachse
	z_loch = floor(2*pi*rm/(3*r_loch));								// Anzahl der Löcher für Material-/Gewichtsersparnis
	
	optimiert = (optimiert && r >= breite*1.5 && d > 2*bohrung);	// ist Optimierung sinnvoll?

	// Zeichnung
	union(){
		rotate([0,0,-phi_r-90*(1-spiel)/zahnzahl]){						// Zahn auf x-Achse zentrieren;
																		// macht Ausrichtung mit anderen Rädern einfacher

			linear_extrude(height = breite, twist = gamma){
				difference(){
					union(){
						zahnbreite = (180*(1-spiel))/zahnzahl+2*phi_r;
						circle(rf);										// Fußkreis	
						for (rot = [0:tau:360]){
							rotate (rot){								// "Zahnzahl-mal" kopieren und drehen
								polygon(concat(							// Zahn
									[[0,0]],							// Zahnsegment beginnt und endet im Ursprung
									[for (rho = [0:schritt:rho_ra])		// von null Grad (Grundkreis)
																		// bis maximalen Evolventenwinkel (Kopfkreis)
										pol_zu_kart(ev(rb,rho))],		// Erste Evolventen-Flanke

									[pol_zu_kart(ev(rb,rho_ra))],		// Punkt der Evolvente auf Kopfkreis

									[for (rho = [rho_ra:-schritt:0])	// von maximalen Evolventenwinkel (Kopfkreis)
																		// bis null Grad (Grundkreis)
										pol_zu_kart([ev(rb,rho)[0], zahnbreite-ev(rb,rho)[1]])]
																		// Zweite Evolventen-Flanke
																		// (180*(1-spiel)) statt 180 Grad,
																		// um Spiel an den Flanken zu erlauben
									)
								);
							}
						}
					}			
					circle(r = rm+r_loch*1.49);							// "Bohrung"
				}
			}
		}
        
		// mit Materialersparnis
		if (optimiert) {
			linear_extrude(height = breite){
				difference(){
						circle(r = (bohrung+r_loch)/2);
						circle(r = bohrung/2);							// Bohrung
					}
				}
			linear_extrude(height = (breite-r_loch/2 < breite*2/3) ? breite*2/3 : breite-r_loch/2){
				difference(){
                 
					circle(r=rm+r_loch*1.51);
					union(){
						circle(r=(bohrung+r_loch)/2);
						for (i = [0:1:z_loch]){
							translate(kugel_zu_kart([rm,90,i*360/z_loch]))
								circle(r = r_loch);
						}
					}
				}
			}
		}
		// ohne Materialersparnis
		else {

			linear_extrude(height = breite){
				difference(){
					circle(r = rm+r_loch*1.51);
					circle(r = bohrung/2);
				}
			}
		}
	}
}



        //passfederbreite=5.25; (gut wackelt aber)
    passfederbreite=5.10;
    passfederueberstand=3;
    



%cylinder(d=85,h=10);
//cylinder(d=130,h=5);
difference(){
    
    stirnrad(modul, zahnzahl, breite, bohrung, eingriffswinkel, schraegungswinkel, optimiert);
    

    translate ([-passfederbreite/2, 0, -1])
       cube ([passfederbreite, bohrung/2+passfederueberstand, 50]);
    cylinder(d=26,h=6.5);
    
    //bohrungen
    abstand=28;
    translate ([0, -abstand, 0])
    cylinder(d=9,h=30);
    translate ([0, abstand, 0])
    cylinder(d=9,h=30);
    //senkungen
        translate ([0, -abstand, 0])
        cylinder(d=14,h=8.6);
        translate ([0, abstand, 0])
        cylinder(d=14,h=8.6);
   
    
}



//RING
//    translate ([0, 0, breite])
//    difference(){
//        cylinder(d=30,h=35.5-breite);
 //       cylinder(d=bohrung,h=35.5-breite+1);
 //       translate ([-passfederbreite/2, 0, -1])
 //           cube ([passfederbreite, bohrung/2+passfederueberstand, 50]);
  //  }
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
//%cylinder(h=10, d=100; center = true);