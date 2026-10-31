
  google.charts.load('current', {'packages':['corechart']});
  google.charts.setOnLoadCallback(drawChart);
  function drawChart() {
  var data = new google.visualization.DataTable();
  data.addColumn('date', 'Date');
  data.addColumn('number', 'pH');
  data.addColumn('number', 'ec');
  data.addColumn('number', 'wt');
  data.addRows(%datatable0_html%);

  var options = {
 'curveType': 'function',
 'legend': 'none',
 'chartArea': {height:"80%"}
   };

 var myView0 = new google.visualization.DataView(data);
 myView0.setColumns([0,1]);
 var myView1 = new google.visualization.DataView(data);
 myView1.setColumns([0,2]);
 var myView2 = new google.visualization.DataView(data);
 myView2.setColumns([0,3]);
 
 var phchart = new google.visualization.LineChart(document.getElementById('ph_chart'));
 var ecchart = new google.visualization.LineChart(document.getElementById('ec_chart'));
 var wtchart = new google.visualization.LineChart(document.getElementById('wt_chart'));
 phchart.draw(myView0, options);
 ecchart.draw(myView1, options);
 wtchart.draw(myView2, options);
 }
