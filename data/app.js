window.onload = function() {
   getData();
}

function getData() {
	const config = {
		apiKey: "AIzaSyAF1DwMIDgX9gMYinN63EEJ0Xc9jd5PetA",
		authDomain: "biosense-android-version.firebaseapp.com",
		databaseURL: "https://biosense-android-version.firebaseio.com",
		storageBucket: "biosense-android-version.appspot.com"
	};
	firebase.initializeApp(config);

	// Get a reference to the database service
	var database = firebase.database();

	const preObject = document.getElementById('object');

	var dbRefCO2 = firebase.database().ref().child('Sensors').child('DailyMeasurements').child('CO2');
		
	var dbRefHumidity = firebase.database().ref().child('Sensors').child('DailyMeasurements').child('Humidity');

	var dbRefPressure = firebase.database().ref().child('Sensors').child('DailyMeasurements').child('Pressure');

	var dbRefTVOC = firebase.database().ref().child('Sensors').child('DailyMeasurements').child('TVOC');

	var dbRefTemperature = firebase.database().ref().child('Sensors').child('DailyMeasurements').child('Temperature');

	dbRefCO2.limitToLast(1).on('child_added', snap => 
	document.getElementById("valueCO2").innerHTML = snap.val().value + " " + snap.val().measure_unit
	);
	dbRefHumidity.limitToLast(1).on('child_added', snap => 
	document.getElementById("valueHumidity").innerHTML = snap.val().value + " " + snap.val().measure_unit
	);	
	dbRefPressure.limitToLast(1).on('child_added', snap => 
	document.getElementById("valuePressure").innerHTML = snap.val().value + " " + snap.val().measure_unit
	);
	dbRefTVOC.limitToLast(1).on('child_added', snap => 
	document.getElementById("valueTVOC").innerHTML = snap.val().value + " " + snap.val().measure_unit
	);
	dbRefTemperature.limitToLast(1).on('child_added', snap => 
	document.getElementById("valueTemperature").innerHTML = snap.val().value + " " + snap.val().measure_unit
	);
	
	
};